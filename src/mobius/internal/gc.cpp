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
// Per-thread registry segments + object pools.
//
// Every thread that allocates traced objects gets a GcThread: a registry
// segment (circular list with sentinel) plus per-type fixed-size chunk pools
// (slab-backed free lists). The owning thread links and unlinks its segment
// with plain stores — no locks, no atomics. The collector walks all segments
// only at quiescent safepoints (no other script thread is running), so the
// walks need no synchronization either.
//
// The one cross-thread case is an object allocated on thread A whose last
// reference is dropped on thread B (channel/future transfer holders). B may
// not touch A's list, so B pushes the header onto A's MPSC pending queue and
// QUARANTINES the chunk (it enters no free list): the memory must stay
// intact until the header is unlinked, and the header must not be revived by
// reuse while still linked. A drains its queue on its next allocation; the
// collector drains every queue at the start of a quiescent walk. Whoever
// drains takes the chunk into its own pool.
//
// Slabs are carved from malloc in 64KB blocks and never returned (chunks
// recycle forever; trimming is future work). GcThread records are
// intentionally leaked on thread exit — dead threads' segments may still
// hold live objects, and the sweeper keeps servicing them.
// ============================================================================

// MOBIUS_GC_NO_POOL=1: route chunks through the global heap (diagnostics —
// ASan then sees each object's exact lifetime instead of pool recycling).
static int g_gc_no_pool = []() {
    const char* e = getenv("MOBIUS_GC_NO_POOL");
    return e ? atoi(e) : 0;
}();

namespace {

constexpr int    GC_TYPE_COUNT = 4;
constexpr size_t GC_SLAB_SIZE  = 64 * 1024;

struct GcPending {
    GcHeader*  h;
    GcPending* next;
};

struct GcPool {
    void* free_head = nullptr;
    char* cursor = nullptr;
    char* end = nullptr;
};

struct GcThread {
    GcHeader sentinel;                            // segment list head
    std::atomic<GcPending*> pending{nullptr};     // cross-thread deferred unlinks
    size_t count = 0;                             // linked headers (owner-written)
    size_t allocs_since_gc = 0;
    GcPool pools[GC_TYPE_COUNT];
    // In-flight cross-thread frees on THIS thread (LIFO: an inner delete
    // completes — destructor AND operator delete — before the outer
    // operator delete runs, so nested frees pop in order). gc_untrack
    // records the corpse here; gc_object_free hands it to the owner.
    struct DeferredFree { void* obj; GcHeader* h; };
    std::vector<DeferredFree> defer_stack;
    GcThread* next_thread = nullptr;              // directory linkage
    GcThread() {
        sentinel.prev = &sentinel;
        sentinel.next = &sentinel;
    }
};

struct GcDirectory {
    std::mutex mutex;            // guards the thread list only (cold paths)
    GcThread* head = nullptr;
};
GcDirectory& directory() { static GcDirectory d; return d; }

// Effective allocation budget between collections; written only by the
// (quiescent) sweeper, read racily by allocating threads. 0 = uninitialized.
volatile size_t g_gc_budget = 0;

thread_local GcThread* tl_gc = nullptr;

size_t gc_threshold_base_fwd();

GcThread* gc_thread_slow() {
    GcThread* t = new GcThread();     // leaked deliberately (see file comment)
    GcDirectory& d = directory();
    std::lock_guard<std::mutex> lock(d.mutex);
    t->next_thread = d.head;
    d.head = t;
    if (g_gc_budget == 0) g_gc_budget = gc_threshold_base_fwd();
    tl_gc = t;
    return t;
}

inline GcThread* gc_thread() {
    GcThread* t = tl_gc;
    return t ? t : gc_thread_slow();
}

inline void segment_unlink(GcHeader* h) {
    h->prev->next = h->next;
    h->next->prev = h->prev;
    h->prev = h->next = nullptr;
}

inline void pool_push(GcThread* t, GcObjectType type, void* chunk) {
    GcPool& p = t->pools[type];
    *(void**)chunk = p.free_head;   // free-list link lives at offset 0; the
    p.free_head = chunk;            // GcHeader member is elsewhere and stays
}

// Drain a thread's pending queue: unlink each corpse from ITS segment and
// take the chunk into `self`'s pool. Callers are the queue's owner (from
// gc_track, self == owner) or the collector at quiescence — never
// concurrent. `self` must be resolved by the CALLER: resolving it here via
// gc_thread() can call gc_thread_slow(), which takes the directory mutex —
// and gc_drain_all_pending already holds it (observed as a self-deadlock
// when the script fiber migrated to a fresh worker thread and its first
// safepoint there collected).
void gc_drain_pending(GcThread* owner, GcThread* self) {
    GcPending* rec = owner->pending.exchange(nullptr, std::memory_order_acquire);
    while (rec) {
        GcHeader* h = rec->h;
        if (h->prev) { segment_unlink(h); owner->count--; }
        if (MOBIUS_UNLIKELY(g_gc_no_pool)) free(h->obj);
        else pool_push(self, h->type(), h->obj);
        GcPending* next = rec->next;
        free(rec);
        rec = next;
    }
}

// Quiescent contexts only: drain every thread's queue so whole-registry
// walks never see a destructed corpse still linked.
void gc_drain_all_pending() {
    GcThread* self = gc_thread();   // BEFORE the lock — may register a thread
    GcDirectory& d = directory();
    std::lock_guard<std::mutex> lock(d.mutex);
    for (GcThread* t = d.head; t; t = t->next_thread)
        if (t->pending.load(std::memory_order_relaxed)) gc_drain_pending(t, self);
}

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

namespace { size_t gc_threshold_base_fwd() { return gc_threshold_base(); } }

void* gc_object_alloc(GcObjectType type, size_t sz) {
    if (MOBIUS_UNLIKELY(g_gc_no_pool)) return malloc(sz);
    GcThread* t = gc_thread();
    GcPool& p = t->pools[type];
    if (void* c = p.free_head) {
        p.free_head = *(void**)c;
        return c;
    }
    size_t chunk = (sz + 15) & ~size_t(15);
    if ((size_t)(p.end - p.cursor) < chunk) {
        char* slab = (char*)malloc(GC_SLAB_SIZE);
        if (!slab) return nullptr;
        p.cursor = slab;
        p.end = slab + GC_SLAB_SIZE;
    }
    void* c = p.cursor;
    p.cursor += chunk;
    return c;
}

void gc_object_free(GcObjectType type, void* ptr) {
    GcThread* t = gc_thread();
    if (!t->defer_stack.empty() && t->defer_stack.back().obj == ptr) {
        // Cross-thread free: destruction is complete, so NOW hand the corpse
        // to its owner, which unlinks the header and recycles the chunk.
        GcHeader* h = t->defer_stack.back().h;
        t->defer_stack.pop_back();
        GcPending* rec = (GcPending*)malloc(sizeof(GcPending));
        GcThread* owner = (GcThread*)h->owner;
        rec->h = h;
        rec->next = owner->pending.load(std::memory_order_relaxed);
        while (!owner->pending.compare_exchange_weak(rec->next, rec,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed)) {
        }
        return;
    }
    if (MOBIUS_UNLIKELY(g_gc_no_pool)) { free(ptr); return; }
    pool_push(t, type, ptr);
}

void gc_track(GcHeader* h, GcObjectType type, void* obj) {
    GcThread* t = gc_thread();
    if (MOBIUS_UNLIKELY(t->pending.load(std::memory_order_relaxed) != nullptr))
        gc_drain_pending(t, t);
    h->flags = (uint32_t)type;
    h->obj = obj;
    h->owner = t;
    h->prev = t->sentinel.prev;
    h->next = &t->sentinel;
    t->sentinel.prev->next = h;
    t->sentinel.prev = h;
    t->count++;
    if (++t->allocs_since_gc >= g_gc_budget) g_gc_pending = true;
}

void gc_untrack(GcHeader* h) {
    if (!h->prev) return;   // already unlinked by the sweep
    GcThread* t = gc_thread();
    if (MOBIUS_LIKELY(h->owner == t)) {
        segment_unlink(h);
        t->count--;
        return;
    }
    // Foreign thread: the owner must do the unlink, but NOT YET — this call
    // runs at the top of the destructor sequence, and member destructors are
    // still about to run on this memory. Handing the corpse over now would
    // let the owner recycle the chunk mid-destruction (observed as a
    // use-after-free under the web-module suite). Record it; the matching
    // gc_object_free — after destruction has fully completed — does the
    // handoff. Until then the header stays linked in the owner's segment,
    // which is safe: segment walks happen only at quiescence, and this
    // thread destructing means we are not quiescent.
    t->defer_stack.push_back({h->obj, h});
}

size_t gc_tracked_count() {
    // Sums per-segment counters rather than walking the lists: owner threads
    // may be linking concurrently, and chasing their pointers would race.
    // Reading the integers races too, but only approximately (introspection).
    GcDirectory& d = directory();
    std::lock_guard<std::mutex> lock(d.mutex);
    size_t n = 0;
    for (GcThread* t = d.head; t; t = t->next_thread) n += t->count;
    return n;
}

// QUIESCENT CALLERS ONLY (collector, shadow verifier, tests at settle
// points): walks every segment's raw links, which owner threads mutate
// lock-free — concurrent script execution would race the traversal.
void gc_for_each_tracked(GcVisitFn cb, void* ud) {
    GcDirectory& d = directory();
    std::lock_guard<std::mutex> lock(d.mutex);
    for (GcThread* t = d.head; t; t = t->next_thread)
        for (GcHeader* h = t->sentinel.next; h != &t->sentinel; h = h->next)
            cb(h, ud);
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
    gc_drain_all_pending();
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
    for (GcHeader* h : dead) gc_object_free(h->type(), h->obj);
}

// MOBIUS_GC_LOG=1: cumulative collection stats printed at teardown.
static int g_gc_log = []() {
    const char* e = getenv("MOBIUS_GC_LOG");
    return e ? atoi(e) : 0;
}();
static std::atomic<uint64_t> g_gc_collections{0}, g_gc_freed{0}, g_gc_mark_ns{0}, g_gc_sweep_ns{0};

size_t gc_collect(MobiusVM* vm) {
    auto t0 = std::chrono::steady_clock::now();
    gc_drain_all_pending();   // corpses must be unlinked before any walk
    gc_mark_from_roots(vm);
    auto t1 = std::chrono::steady_clock::now();

    SweepCtx sweep;
    size_t live = 0, allocs = 0;
    {
        GcDirectory& d = directory();
        std::lock_guard<std::mutex> lock(d.mutex);
        for (GcThread* t = d.head; t; t = t->next_thread) {
            GcHeader* h = t->sentinel.next;
            while (h != &t->sentinel) {
                GcHeader* next = h->next;
                if (h->marked()) {
                    h->setMarked(false);   // keep the all-clear invariant
                    live++;
                } else {
                    // Unlink now; destructors find prev == nullptr and skip.
                    segment_unlink(h);
                    t->count--;
                    sweep.dead.push_back(h);
                }
                h = next;
            }
            allocs += t->allocs_since_gc;
            t->allocs_since_gc = 0;
        }
        size_t base = gc_threshold_base();
        size_t floor_ = live > base ? live : base;
        if (sweep.dead.size() * 8 < allocs) {
            // Mostly acyclic churn already reclaimed by refcounting: this
            // collection was wasted work, so wait longer next time.
            size_t doubled = g_gc_budget * 2;
            g_gc_budget = doubled > floor_ ? doubled : floor_;
        } else {
            g_gc_budget = floor_;
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
    gc_drain_all_pending();
    SweepCtx sweep;
    {
        GcDirectory& d = directory();
        std::lock_guard<std::mutex> lock(d.mutex);
        for (GcThread* t = d.head; t; t = t->next_thread) {
            GcHeader* h = t->sentinel.next;
            while (h != &t->sentinel) {
                GcHeader* next = h->next;
                segment_unlink(h);
                t->count--;
                sweep.dead.push_back(h);
                h = next;
            }
            t->allocs_since_gc = 0;
        }
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
