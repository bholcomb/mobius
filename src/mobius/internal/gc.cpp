#include "internal/gc.h"

#include "data/value.h"
#include "data/table.h"
#include "data/array.h"
#include "data/array_slice.h"
#include "data/channel.h"
#include "data/future.h"
#include "data/shared_cell.h"
#include "data/function.h"
#include "vm/vm.h"     // Upvalue
#include "state/mobius_state.h"
#include "plugin/module_registry.h"
#include "fiber/job_system.h"
#include "frontend/ast.h"
#include <chrono>

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <mutex>

// ============================================================================
// Global registry — a circular doubly-linked list with a sentinel head, so
// unlink never touches the head pointer. One mutex guards all link/unlink:
// contention is only possible across fibers, and both the free-side unlink and
// this lock are stage-1 scaffolding that the sweep eventually replaces.
// ============================================================================

namespace {
struct GcRegistry {
    GcHeader sentinel;
    size_t count = 0;
    size_t allocs_since_gc = 0;
    size_t budget = SIZE_MAX;   // finalized on first gc_track
    std::mutex mutex;
    GcRegistry() {
        sentinel.prev = &sentinel;
        sentinel.next = &sentinel;
    }
};
GcRegistry& registry() {
    static GcRegistry r;
    return r;
}
// Object pointers are recovered from headers via a side pointer stored at
// track() time (obj field), avoiding offsetof on non-standard-layout types.
} // namespace

// Set when allocations since the last collection exceed the threshold;
// checked (cheaply) at VM safepoints. Benign race: worst case a collection
// happens one safepoint later. Starts armed under MOBIUS_GC_STRESS so the
// first safepoint already collects.
volatile bool g_gc_pending = []() {
    const char* e = getenv("MOBIUS_GC_STRESS");
    return e && atoi(e) != 0;
}();

// Base allocation budget between collections. The effective budget is
// max(base, live objects after the last collection): allocation-churn
// programs collect while the garbage is still cache-hot, while programs
// with big stable heaps don't pay an O(live) mark every few thousand
// allocations.
static size_t gc_threshold_base() {
    static size_t t = []() {
        const char* e = getenv("MOBIUS_GC_THRESHOLD");
        long v = e ? atol(e) : 0;
        return (size_t)(v > 0 ? v : 2000);
    }();
    return t;
}

void gc_track(GcHeader* h, GcObjectType type, void* obj) {
    h->flags = (uint32_t)type;
    h->obj = obj;
    GcRegistry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    h->prev = r.sentinel.prev;
    h->next = &r.sentinel;
    r.sentinel.prev->next = h;
    r.sentinel.prev = h;
    r.count++;
    if (r.budget == SIZE_MAX) r.budget = gc_threshold_base();
    if (++r.allocs_since_gc >= r.budget) g_gc_pending = true;
}

void gc_untrack(GcHeader* h) {
    GcRegistry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (!h->prev) return;   // already unlinked by the sweep
    h->prev->next = h->next;
    h->next->prev = h->prev;
    h->prev = h->next = nullptr;
    r.count--;
}

size_t gc_tracked_count() {
    GcRegistry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.count;
}

void gc_for_each_tracked(GcVisitFn cb, void* ud) {
    GcRegistry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    for (GcHeader* h = r.sentinel.next; h != &r.sentinel; h = h->next) {
        cb(h, ud);
    }
}

// ============================================================================
// Traversal
// ============================================================================

// Recover the owning object from a header (stored at track() time).
static Table*          hdr_table(GcHeader* h)    { return (Table*)h->obj; }
static ArrayValue*     hdr_array(GcHeader* h)    { return (ArrayValue*)h->obj; }
static MobiusFunction* hdr_function(GcHeader* h) { return (MobiusFunction*)h->obj; }
static Upvalue*        hdr_upvalue(GcHeader* h)  { return (Upvalue*)h->obj; }

struct ValueVisitCtx {
    GcVisitFn cb;
    void* ud;
    int depth;            // pass-through recursion guard (cells in cells, ...)
};

static void visit_value(const Value& v, ValueVisitCtx* ctx);

static void visit_value_thunk(const Value& v, void* ud) {
    visit_value(v, (ValueVisitCtx*)ud);
}

static void visit_value(const Value& v, ValueVisitCtx* ctx) {
    switch (v.type) {
        case VAL_TABLE:
            if (v.as.table) ctx->cb(v.as.table->gcHeader(), ctx->ud);
            break;
        case VAL_ARRAY:
            if (v.as.array) ctx->cb(v.as.array->gcHeader(), ctx->ud);
            break;
        case VAL_FUNCTION:
            if (v.as.function) ctx->cb(&v.as.function->gc_, ctx->ud);
            break;
        // Refcounted pass-through holders: not traced themselves, but they can
        // hold traced objects, so the marker must see through them.
        case VAL_ARRAY_SLICE:
            if (v.as.array_slice && v.as.array_slice->parent())
                ctx->cb(v.as.array_slice->parent()->gcHeader(), ctx->ud);
            break;
        case VAL_SHARED_CELL:
            if (v.as.shared_cell && ctx->depth < 16) {
                ctx->depth++;
                Value inner = v.as.shared_cell->load();
                visit_value(inner, ctx);
                ctx->depth--;
            }
            break;
        case VAL_CHANNEL:
            if (v.as.channel && ctx->depth < 16) {
                ctx->depth++;
                v.as.channel->forEachBuffered(visit_value_thunk, ctx);
                ctx->depth--;
            }
            break;
        case VAL_FUTURE:
            if (v.as.future && ctx->depth < 16) {
                ctx->depth++;
                visit_value(v.as.future->result(), ctx);
                visit_value(v.as.future->error(), ctx);
                ctx->depth--;
            }
            break;
        default:
            break;
    }
}

void gc_visit_value_children(const Value& v, GcVisitFn cb, void* ud) {
    ValueVisitCtx ctx{cb, ud, 0};
    visit_value(v, &ctx);
}

void gc_traverse_children(GcHeader* h, GcVisitFn cb, void* ud) {
    ValueVisitCtx ctx{cb, ud, 0};
    switch (h->type()) {
        case GC_TABLE: {
            Table* t = hdr_table(h);
            const auto& entries = t->entries();
            const auto& tags = t->tags();
            for (size_t i = 0; i < entries.size(); i++) {
                if (tags[i] != Table::TAG_EMPTY) {
                    visit_value(entries[i].key, &ctx);
                    visit_value(entries[i].value, &ctx);
                }
            }
            if (t->getMetatable()) cb(t->getMetatable()->gcHeader(), ud);
            // The metamethod cache holds a counted reference of its own.
            visit_value(t->mmCacheValue(), &ctx);
            break;
        }
        case GC_ARRAY: {
            ArrayValue* a = hdr_array(h);
            for (size_t i = 0; i < a->length(); i++)
                visit_value(a->unsafeGet(i), &ctx);
            break;
        }
        case GC_FUNCTION: {
            MobiusFunction* fn = hdr_function(h);
            if (fn->upvalues) {
                for (int i = 0; i < fn->upvalue_count; i++)
                    if (fn->upvalues[i]) cb(&fn->upvalues[i]->gc_, ud);
            }
            break;
        }
        case GC_UPVALUE: {
            Upvalue* uv = hdr_upvalue(h);
            // An open upvalue's location points into VM registers, which are
            // roots in their own right; only the closed value is a child.
            if (!uv->is_open) visit_value(uv->closed, &ctx);
            break;
        }
    }
}

// ============================================================================
// Shadow verification (stage 2)
//
// At a quiescent bytecode boundary: enumerate every root, mark transitively,
// then check that each refcount-live tracked object was reached. A violation
// means the root enumeration missed something — found while refcounting still
// guarantees correctness. MOBIUS_GC_SHADOW=1 reports, =2 reports and aborts.
// ============================================================================

int g_gc_shadow_mode = []() {
    const char* e = getenv("MOBIUS_GC_SHADOW");
    return e ? atoi(e) : 0;
}();

// MOBIUS_GC_STRESS=1: collect at every eligible safepoint. With ASan on top,
// any object the collector frees while still in use turns into a hard fault
// at the exact use site — the post-flip root-coverage test.
static int g_gc_stress = []() {
    const char* e = getenv("MOBIUS_GC_STRESS");
    return e ? atoi(e) : 0;
}();

void gc_shadow_init_from_env() { /* initialized at load; kept for API symmetry */ }

namespace {

struct MarkCtx {
    std::vector<GcHeader*> worklist;
};

void mark_header(GcHeader* h, void* ud) {
    if (!h || h->marked()) return;
    h->setMarked(true);
    ((MarkCtx*)ud)->worklist.push_back(h);
}

void mark_value(const Value& v, void* ud) {
    gc_visit_value_children(v, mark_header, ud);
}

void mark_table(Table* t, void* ud) {
    if (t) mark_header(t->gcHeader(), ud);
}

void clear_mark_cb(GcHeader* h, void* ud) {
    (void)ud;
    h->setMarked(false);
}

struct CheckCtx {
    std::vector<GcHeader*> unmarked;
};

void collect_unmarked_cb(GcHeader* h, void* ud) {
    if (!h->marked()) ((CheckCtx*)ud)->unmarked.push_back(h);
}

} // namespace

// Shared mark phase: clear marks, then mark everything reachable from the
// enumerated roots. Verified against refcount liveness by the whole test
// suite under MOBIUS_GC_SHADOW=2 before the collector was allowed to free.
// Invariant: every tracked object has its mark bit CLEAR between passes —
// gc_track() starts objects clear, the sweep clears survivors, and the
// shadow verifier clears after itself. So no O(heap) clear pass here.
static void gc_mark_from_roots(MobiusVM* vm) {
    MobiusState* state = vm->state_;

    MarkCtx ctx;
    // 1. The VM's full register file. Deliberately not limited to the live
    //    frame range: under refcounting, stale registers above the frame top
    //    legitimately pin objects, and this pass verifies against refcount
    //    liveness. (The eventual collector scans only the live range — that
    //    is a policy improvement, not a soundness requirement.)
    for (const Value& v : vm->registers_) mark_value(v, &ctx);
    // 2. Upvalues tracked by live frames.
    for (size_t d = 0; d <= vm->call_depth_; d++) {
        CallInfo& ci = vm->call_stack_[d];
        for (int i = 0; i < ci.upvalue_count; i++)
            if (ci.upvalues[i]) mark_header(&ci.upvalues[i]->gc_, &ctx);
    }
    // 3. State-held roots: globals, C-API refs, type/userdata metatables.
    state->gcVisitRoots(mark_value, mark_table, &ctx);
    // 4. Module environments in the global registry.
    getGlobalRegistry()->forEachGlobalValue(mark_value, &ctx);

    // Drain.
    while (!ctx.worklist.empty()) {
        GcHeader* h = ctx.worklist.back();
        ctx.worklist.pop_back();
        gc_traverse_children(h, mark_header, &ctx);
    }
}

void gc_shadow_verify_now(MobiusVM* vm) {
    // Pre-flip this compared reachability against refcount liveness and
    // aborted on any refcount-live object the roots missed; that proof gated
    // enabling the collector. Post-flip the traced types no longer tick their
    // counts, so the rc oracle is gone — this is now a reachability report.
    // The load-bearing successor is MOBIUS_GC_STRESS=1 (collect at every
    // safepoint) run under ASan: a missed root becomes a use-after-free there.
    gc_mark_from_roots(vm);

    CheckCtx check;
    gc_for_each_tracked(collect_unmarked_cb, &check);
    if (!check.unmarked.empty() && g_gc_shadow_mode >= 1)
        fprintf(stderr, "[gc-shadow] %zu unreachable object(s) pending collection\n",
                check.unmarked.size());
    gc_for_each_tracked(clear_mark_cb, nullptr);   // restore the all-clear invariant
}


// ============================================================================
// The collector (stage 4)
//
// Synchronous mark-sweep at a quiescent bytecode boundary: no script is
// executing anywhere else (outstanding-jobs == 0), no native call is in
// flight on this VM, so the enumerated roots are complete — the property the
// shadow verifier proved across the test suite. Sweep first UNLINKS every
// unmarked object from the registry, then destroys: destructors run
// arbitrary Value releases and must not touch freed neighbors' list links.
// ============================================================================

namespace {

struct SweepCtx {
    std::vector<GcHeader*> dead;
};

} // namespace

// Destruction is two-pass. A dead object's destructor releases its child
// Values, which ticks refcounts on sibling dead objects (a cycle dies
// together by definition). Pass 1 runs every destructor while all dead
// memory is still allocated, so those ticks land in live allocations;
// pass 2 frees the raw memory.
static void gc_destruct(GcHeader* h) {
    switch (h->type()) {
        case GC_TABLE:
            ((Table*)h->obj)->~Table();
            break;
        case GC_ARRAY:
            ((ArrayValue*)h->obj)->~ArrayValue();
            break;
        case GC_FUNCTION: {
            MobiusFunction* fn = (MobiusFunction*)h->obj;
            mobius_function_teardown(fn);
            fn->~MobiusFunction();
            break;
        }
        case GC_UPVALUE:
            ((Upvalue*)h->obj)->~Upvalue();
            break;
    }
}

static volatile bool g_gc_in_sweep = false;

bool gc_is_sweeping() { return g_gc_in_sweep; }

// Out-of-line cold path of RefCounted::release() (see ref_counted.h): the
// count hit zero. Defined here so the sweep flag never appears in inline
// code compiled into plugins.
void RefCounted::releaseAtZero() {
    if (gc_managed_ && g_gc_in_sweep) return;   // the sweep owns freeing it
    delete this;
}

static void gc_free_dead(std::vector<GcHeader*>& dead) {
    g_gc_in_sweep = true;
    for (GcHeader* h : dead) gc_destruct(h);
    g_gc_in_sweep = false;
    for (GcHeader* h : dead) ::operator delete(h->obj);
}

// MOBIUS_GC_LOG=1: cumulative collection stats printed at teardown.
static int g_gc_log = []() {
    const char* e = getenv("MOBIUS_GC_LOG");
    return e ? atoi(e) : 0;
}();
static std::atomic<uint64_t> g_gc_collections{0}, g_gc_freed{0}, g_gc_mark_ns{0}, g_gc_sweep_ns{0};

size_t gc_collect(MobiusVM* vm) {
    auto t0 = std::chrono::steady_clock::now();
    gc_mark_from_roots(vm);
    auto t1 = std::chrono::steady_clock::now();

    SweepCtx sweep;
    {
        GcRegistry& r = registry();
        std::lock_guard<std::mutex> lock(r.mutex);
        GcHeader* h = r.sentinel.next;
        while (h != &r.sentinel) {
            GcHeader* next = h->next;
            if (h->marked()) {
                h->setMarked(false);   // keep the all-clear invariant
            } else {
                // Unlink now; destructors then find prev == nullptr and skip.
                h->prev->next = h->next;
                h->next->prev = h->prev;
                h->prev = h->next = nullptr;
                r.count--;
                sweep.dead.push_back(h);
            }
            h = next;
        }
        size_t allocs = r.allocs_since_gc;
        r.allocs_since_gc = 0;
        size_t base = gc_threshold_base();
        size_t floor_ = r.count > base ? r.count : base;
        if (sweep.dead.size() * 8 < allocs) {
            // Mostly acyclic churn already reclaimed by refcounting: this
            // collection was wasted work, so wait longer next time.
            size_t doubled = r.budget * 2;
            r.budget = doubled > floor_ ? doubled : floor_;
        } else {
            r.budget = floor_;
        }
    }
    g_gc_pending = (bool)g_gc_stress;   // stress mode keeps the hooks hot

    gc_free_dead(sweep.dead);
    if (g_gc_log) {
        auto t2 = std::chrono::steady_clock::now();
        g_gc_collections++;
        g_gc_freed += sweep.dead.size();
        g_gc_mark_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        g_gc_sweep_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
    }
    return sweep.dead.size();
}

// Free every remaining tracked object regardless of reachability — state
// teardown, after roots have been cleared and before the string pool dies
// (destructors release string references).
size_t gc_collect_all_for_teardown() {
    if (g_gc_log && g_gc_collections.load())
        fprintf(stderr, "[gc] %llu collections, %llu freed, mark %.1fms, sweep %.1fms\n",
                (unsigned long long)g_gc_collections.load(),
                (unsigned long long)g_gc_freed.load(),
                g_gc_mark_ns.load() / 1e6, g_gc_sweep_ns.load() / 1e6);
    SweepCtx sweep;
    {
        GcRegistry& r = registry();
        std::lock_guard<std::mutex> lock(r.mutex);
        GcHeader* h = r.sentinel.next;
        while (h != &r.sentinel) {
            GcHeader* next = h->next;
            h->prev->next = h->next;
            h->next->prev = h->prev;
            h->prev = h->next = nullptr;
            r.count--;
            sweep.dead.push_back(h);
            h = next;
        }
        r.allocs_since_gc = 0;
    }
    gc_free_dead(sweep.dead);
    return sweep.dead.size();
}

// The VM safepoint: called from loop back-edges and call entry when either
// shadow mode is active or allocation pressure requests a collection.
void gc_safepoint(MobiusVM* vm) {
    if (!vm || !vm->state_) return;
    if (vm->native_depth_ > 0) return;
    JobSystem* js = vm->state_->jobSystem();
    if (js && js->outstandingJobs() != 0) return;

    if (g_gc_pending | g_gc_stress) gc_collect(vm);

    if (g_gc_shadow_mode) {
        static thread_local uint32_t countdown = 0;
        if (countdown++ % 64 == 0) gc_shadow_verify_now(vm);
    }
}
