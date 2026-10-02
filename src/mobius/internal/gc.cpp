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
// Every MobiusState owns a GcHeap. Every thread that allocates traced
// objects for a heap gets a GcSegment of it: a registry list (circular, with
// sentinel). Separately, every thread has a GcThread holding per-type
// fixed-size chunk pools (slab-backed free lists), shared by all heaps. The
// owning thread links and unlinks its segment with plain stores — no locks,
// no atomics. The collector walks one heap's segments, only at that state's
// quiescent safepoints (no other script thread of the state is running), so
// the walks need no synchronization either. Other states keep running: their
// objects are in other heaps.
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
// intentionally leaked on thread exit. Segments live as long as their heap:
// a dead thread's segment may still hold live objects, and the sweeper keeps
// servicing it.
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

// Per OS thread: chunk pools (shared by every heap: a chunk is just memory)
// and the in-flight cross-thread frees.
struct GcThread {
    GcPool pools[GC_TYPE_COUNT];
    // In-flight cross-thread frees on THIS thread (LIFO: an inner delete
    // completes — destructor AND operator delete — before the outer
    // operator delete runs, so nested frees pop in order). gc_untrack
    // records the corpse here; gc_object_free hands it to the owner.
    struct DeferredFree { void* obj; GcHeader* h; };
    std::vector<DeferredFree> defer_stack;
};

} // namespace

// Per (heap, thread): the registry segment linking the objects this thread
// allocated for that heap.
struct GcSegment {
    GcHeader sentinel;                            // segment list head
    std::atomic<GcPending*> pending{nullptr};     // cross-thread deferred unlinks
    size_t count = 0;                             // linked headers (owner-written)
    size_t allocs_since_gc = 0;
    size_t budget = 0;                            // copy of the heap's, for gc_track
    GcThread* thread = nullptr;                   // the owning thread
    GcHeap* heap = nullptr;
    GcSegment* next_segment = nullptr;            // heap linkage
    GcSegment() {
        sentinel.prev = &sentinel;
        sentinel.next = &sentinel;
    }
};

// One per MobiusState.
struct GcHeap {
    std::mutex mutex;              // guards the segment list only (cold paths)
    GcSegment* head = nullptr;
    uint64_t id = 0;               // unique for the process lifetime
    size_t budget = 0;             // allocations between collections
    std::atomic<bool> pending{false};
};

namespace {

// Heaps alive now, by id: lets a thread drop its cached segments of heaps
// that have been destroyed.
std::mutex g_heaps_mutex;
std::unordered_set<uint64_t> g_live_heap_ids;
uint64_t g_next_heap_id = 1;

// Heaps over budget; g_gc_pending is set while this is non-zero.
std::atomic<int> g_pending_heaps{0};

thread_local GcThread* tl_gc = nullptr;

GcThread* gc_thread() {
    GcThread* t = tl_gc;
    if (MOBIUS_LIKELY(t != nullptr)) return t;
    t = new GcThread();   // leaked deliberately: its pools hold live chunks
    tl_gc = t;
    return t;
}

// This thread's segment of a heap: the last one used is cached; the rest
// are in a small per-thread list.
struct SegmentRef { uint64_t heap_id; GcSegment* seg; };
thread_local uint64_t tl_seg_heap_id = 0;
thread_local GcSegment* tl_seg = nullptr;
thread_local std::vector<SegmentRef>* tl_segments = nullptr;

size_t gc_threshold_base_fwd();

GcSegment* gc_segment_slow(GcHeap* heap) {
    if (!tl_segments) tl_segments = new std::vector<SegmentRef>();   // leaked with the thread
    GcSegment* seg = nullptr;
    for (const SegmentRef& r : *tl_segments)
        if (r.heap_id == heap->id) { seg = r.seg; break; }
    if (!seg) {
        {
            // Forget segments of destroyed heaps (their memory is gone).
            std::lock_guard<std::mutex> lock(g_heaps_mutex);
            auto& v = *tl_segments;
            for (size_t i = 0; i < v.size();) {
                if (!g_live_heap_ids.count(v[i].heap_id)) { v[i] = v.back(); v.pop_back(); }
                else i++;
            }
        }
        seg = new GcSegment();
        seg->thread = gc_thread();
        seg->heap = heap;
        std::lock_guard<std::mutex> lock(heap->mutex);
        seg->budget = heap->budget;
        seg->next_segment = heap->head;
        heap->head = seg;
        tl_segments->push_back({heap->id, seg});
    }
    tl_seg_heap_id = heap->id;
    tl_seg = seg;
    return seg;
}

inline GcSegment* gc_segment(GcHeap* heap) {
    if (MOBIUS_LIKELY(tl_seg_heap_id == heap->id)) return tl_seg;
    return gc_segment_slow(heap);
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

// Drain a segment's pending queue: unlink each corpse from the segment and
// take the chunk into `self`'s pool. Callers are the segment's owner thread
// (from gc_track) or the collector at quiescence — never concurrent.
void gc_drain_pending(GcSegment* seg, GcThread* self) {
    GcPending* rec = seg->pending.exchange(nullptr, std::memory_order_acquire);
    while (rec) {
        GcHeader* h = rec->h;
        if (h->prev) { segment_unlink(h); seg->count--; }
        if (MOBIUS_UNLIKELY(g_gc_no_pool)) free(h->obj);
        else pool_push(self, h->type(), h->obj);
        GcPending* next = rec->next;
        free(rec);
        rec = next;
    }
}

// Quiescent contexts only: drain every segment's queue so whole-heap walks
// never see a destructed corpse still linked.
void gc_drain_all_pending(GcHeap* heap) {
    GcThread* self = gc_thread();
    std::lock_guard<std::mutex> lock(heap->mutex);
    for (GcSegment* s = heap->head; s; s = s->next_segment)
        if (s->pending.load(std::memory_order_relaxed)) gc_drain_pending(s, self);
}

void gc_mark_heap_pending(GcHeap* heap);

} // namespace

// Set while some heap's allocations since its last collection exceed its
// budget; checked (cheaply) at VM safepoints, which then collect their own
// heap if it is the one over budget. Benign race: worst case a collection
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

namespace {

size_t gc_threshold_base_fwd() { return gc_threshold_base(); }

void gc_mark_heap_pending(GcHeap* heap) {
    if (heap->pending.load(std::memory_order_relaxed)) return;
    if (!heap->pending.exchange(true, std::memory_order_acq_rel)) {
        g_pending_heaps.fetch_add(1, std::memory_order_acq_rel);
        g_gc_pending = true;
    }
}

// The heap has just been collected (or is going away).
void gc_clear_heap_pending(GcHeap* heap) {
    if (heap->pending.exchange(false, std::memory_order_acq_rel)) {
        if (g_pending_heaps.fetch_sub(1, std::memory_order_acq_rel) == 1)
            g_gc_pending = false;
    }
}

} // namespace

GcHeap* gc_heap_create() {
    GcHeap* heap = new GcHeap();
    heap->budget = gc_threshold_base();
    std::lock_guard<std::mutex> lock(g_heaps_mutex);
    heap->id = g_next_heap_id++;
    g_live_heap_ids.insert(heap->id);
    return heap;
}

void gc_heap_destroy(GcHeap* heap) {
    if (!heap) return;
    {
        std::lock_guard<std::mutex> lock(g_heaps_mutex);
        g_live_heap_ids.erase(heap->id);
    }
    gc_clear_heap_pending(heap);
    // This thread's cache; other threads drop theirs on their next miss.
    if (tl_seg_heap_id == heap->id) { tl_seg_heap_id = 0; tl_seg = nullptr; }
    GcSegment* s = heap->head;
    while (s) {
        GcSegment* next = s->next_segment;
        delete s;
        s = next;
    }
    delete heap;
}

GcHeap* gc_heap_of(const GcHeader* h) {
    return h && h->owner ? static_cast<GcSegment*>(h->owner)->heap : nullptr;
}

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
        GcSegment* owner = (GcSegment*)h->owner;
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

void gc_track(GcHeap* heap, GcHeader* h, GcObjectType type, void* obj) {
    GcSegment* s = gc_segment(heap);
    if (MOBIUS_UNLIKELY(s->pending.load(std::memory_order_relaxed) != nullptr))
        gc_drain_pending(s, s->thread);
    h->flags = (uint32_t)type;
    h->obj = obj;
    h->owner = s;
    h->prev = s->sentinel.prev;
    h->next = &s->sentinel;
    s->sentinel.prev->next = h;
    s->sentinel.prev = h;
    s->count++;
    if (MOBIUS_UNLIKELY(++s->allocs_since_gc >= s->budget)) gc_mark_heap_pending(heap);
}

void gc_untrack(GcHeader* h) {
    if (!h->prev) return;   // already unlinked by the sweep
    GcSegment* s = static_cast<GcSegment*>(h->owner);
    if (MOBIUS_LIKELY(s->thread == tl_gc)) {
        segment_unlink(h);
        s->count--;
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
    gc_thread()->defer_stack.push_back({h->obj, h});
}

size_t gc_tracked_count(GcHeap* heap) {
    // Sums per-segment counters rather than walking the lists: owner threads
    // may be linking concurrently, and chasing their pointers would race.
    // Reading the integers races too, but only approximately (introspection).
    std::lock_guard<std::mutex> lock(heap->mutex);
    size_t n = 0;
    for (GcSegment* s = heap->head; s; s = s->next_segment) n += s->count;
    return n;
}

// QUIESCENT CALLERS ONLY (collector, shadow verifier, tests at settle
// points): walks every segment's raw links, which owner threads mutate
// lock-free — concurrent script execution would race the traversal.
void gc_for_each_tracked(GcHeap* heap, GcVisitFn cb, void* ud) {
    std::lock_guard<std::mutex> lock(heap->mutex);
    for (GcSegment* s = heap->head; s; s = s->next_segment)
        for (GcHeader* h = s->sentinel.next; h != &s->sentinel; h = h->next)
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
                if (Table::isLive(tags[i])) {
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
    mark_value(vm->error_value_, &ctx);   // a thrown value still propagating
    // 2. Upvalues tracked by live frames.
    for (size_t d = 0; d <= vm->call_depth_; d++) {
        CallInfo& ci = vm->call_stack_[d];
        for (int i = 0; i < ci.upvalue_count; i++)
            if (ci.upvalues[i]) mark_header(&ci.upvalues[i]->gc_, &ctx);
    }
    // 3. State-held roots: globals, C-API refs, type/userdata metatables.
    state->gcVisitRoots(mark_value, mark_table, &ctx);
    // 4. Module environments of this state.
    if (state->registry()) state->registry()->forEachGlobalValue(mark_value, &ctx);

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
    GcHeap* heap = vm->state_->gcHeap();
    gc_drain_all_pending(heap);
    gc_mark_from_roots(vm);

    CheckCtx check;
    gc_for_each_tracked(heap, collect_unmarked_cb, &check);
    if (!check.unmarked.empty() && g_gc_shadow_mode >= 1)
        fprintf(stderr, "[gc-shadow] %zu unreachable object(s) pending collection\n",
                check.unmarked.size());
    gc_for_each_tracked(heap, clear_mark_cb, nullptr);   // restore the all-clear invariant
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

// Per thread: a sweep runs every destructor on its own thread, and other
// states keep running (and freeing) on theirs.
static thread_local bool g_gc_in_sweep = false;

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
    GcHeap* heap = vm->state_->gcHeap();
    auto t0 = std::chrono::steady_clock::now();
    gc_drain_all_pending(heap);   // corpses must be unlinked before any walk
    gc_mark_from_roots(vm);
    auto t1 = std::chrono::steady_clock::now();

    SweepCtx sweep;
    size_t live = 0, allocs = 0;
    {
        std::lock_guard<std::mutex> lock(heap->mutex);
        for (GcSegment* t = heap->head; t; t = t->next_segment) {
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
            size_t doubled = heap->budget * 2;
            heap->budget = doubled > floor_ ? doubled : floor_;
        } else {
            heap->budget = floor_;
        }
        for (GcSegment* t = heap->head; t; t = t->next_segment) t->budget = heap->budget;
    }
    gc_clear_heap_pending(heap);
    if (g_gc_stress) g_gc_pending = true;   // stress mode keeps the hooks hot

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
size_t gc_collect_all_for_teardown(GcHeap* heap) {
    if (g_gc_log && g_gc_collections.load())
        fprintf(stderr, "[gc] %llu collections, %llu freed, mark %.1fms, sweep %.1fms\n",
                (unsigned long long)g_gc_collections.load(),
                (unsigned long long)g_gc_freed.load(),
                g_gc_mark_ns.load() / 1e6, g_gc_sweep_ns.load() / 1e6);
    gc_drain_all_pending(heap);
    SweepCtx sweep;
    {
        std::lock_guard<std::mutex> lock(heap->mutex);
        for (GcSegment* t = heap->head; t; t = t->next_segment) {
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

    // g_gc_pending says some heap is over budget; collect if it is ours.
    if (vm->state_->gcHeap()->pending.load(std::memory_order_relaxed) | g_gc_stress)
        gc_collect(vm);

    if (g_gc_shadow_mode) {
        static thread_local uint32_t countdown = 0;
        if (countdown++ % 64 == 0) gc_shadow_verify_now(vm);
    }
}
