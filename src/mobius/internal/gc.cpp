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
}

void gc_untrack(GcHeader* h) {
    GcRegistry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
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

static int header_refcount(GcHeader* h) {
    switch (h->type()) {
        case GC_TABLE: return ((Table*)h->obj)->refCount();
        case GC_ARRAY: return ((ArrayValue*)h->obj)->refCount();
        case GC_FUNCTION: return ((MobiusFunction*)h->obj)->ref_count.load();
        case GC_UPVALUE: return ((Upvalue*)h->obj)->refcount.load();
    }
    return -1;
}

struct CheckCtx {
    std::vector<GcHeader*> unmarked;
};

void collect_unmarked_cb(GcHeader* h, void* ud) {
    if (!h->marked()) ((CheckCtx*)ud)->unmarked.push_back(h);
}

struct IndegreeCtx {
    const std::unordered_set<GcHeader*>* unmarked_set;
    std::unordered_map<GcHeader*, int>* indegree;
};

void indegree_cb(GcHeader* child, void* ud) {
    IndegreeCtx* c = (IndegreeCtx*)ud;
    if (c->unmarked_set->count(child)) (*c->indegree)[child]++;
}

} // namespace

void gc_shadow_verify_now(MobiusVM* vm) {
    MobiusState* state = vm->state_;

    gc_for_each_tracked(clear_mark_cb, nullptr);

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

    CheckCtx check;
    gc_for_each_tracked(collect_unmarked_cb, &check);
    if (!check.unmarked.empty()) {
        // An unmarked-but-refcount-live object is one of two things:
        //  * a LEAKED CYCLE — its refcount is fully explained by references
        //    from other unmarked objects. Expected under refcounting (this is
        //    what the collector will reclaim after the flip); informational.
        //  * a MISSED ROOT — some external holder the enumeration didn't
        //    visit. That is the bug shadow mode exists to catch; fatal.
        std::unordered_set<GcHeader*> unmarked_set(check.unmarked.begin(), check.unmarked.end());
        std::unordered_map<GcHeader*, int> indegree;
        IndegreeCtx ic{&unmarked_set, &indegree};
        for (GcHeader* h : check.unmarked)
            gc_traverse_children(h, indegree_cb, &ic);

        size_t missed_roots = 0, cycle_garbage = 0;
        static const char* names[] = {"table", "array", "function", "upvalue"};
        for (GcHeader* h : check.unmarked) {
            int rc = header_refcount(h);
            int in = indegree.count(h) ? indegree[h] : 0;
            if (rc > in) {
                missed_roots++;
                fprintf(stderr, "[gc-shadow] MISSED ROOT: %s %p rc=%d internal-refs=%d\n",
                        names[h->type() & 0x3], h->obj, rc, in);
            } else {
                cycle_garbage++;
            }
        }
        if (cycle_garbage && g_gc_shadow_mode >= 1)
            fprintf(stderr, "[gc-shadow] %zu object(s) in leaked reference cycles (collector will reclaim)\n",
                    cycle_garbage);
        if (missed_roots) {
            fprintf(stderr, "[gc-shadow] %zu missed root(s) — root enumeration is incomplete\n",
                    missed_roots);
            if (g_gc_shadow_mode >= 2) abort();
        }
    }
}

void gc_shadow_maybe_verify(MobiusVM* vm) {
    if (!vm || !vm->state_) return;
    if (vm->native_depth_ > 0) return;              // C++ locals may hold values
    JobSystem* js = vm->state_->jobSystem();
    if (js && js->outstandingJobs() != 0) return;   // other fibers may be mutating
    static thread_local uint32_t countdown = 0;
    if (countdown++ % 64 != 0) return;              // amortize the O(heap) pass
    gc_shadow_verify_now(vm);
}
