#ifndef MOBIUS_GC_H
#define MOBIUS_GC_H

// ============================================================================
// Tracing-GC groundwork (stage 1 of the per-fiber collector).
//
// The end-state design: each fiber owns a heap of the cycle-capable,
// fiber-local types (Table, ArrayValue, MobiusFunction closures, Upvalue) and
// collects it independently at yield/allocation points — sound because a value
// crossing a fiber boundary is deep-copied unless it is `shared`. Everything
// else (SharedCell, Channel, FutureValue, userdata, strings, ...) stays
// reference counted; those are the small cross-fiber "shared domain".
//
// THIS stage only adds the scaffolding, with reference counting still owning
// every free:
//   * every traced-type object carries a GcHeader and is linked into a global
//     registry at construction, unlinked at destruction;
//   * gc_traverse_children() enumerates an object's traced children, and
//     gc_visit_value_children() extracts traced objects from a Value —
//     including THROUGH refcounted pass-through holders (shared cells,
//     channels, futures, slices), which the shadow marker will need.
//
// The registry is one global list guarded by one mutex. That is deliberate
// stage-1 simplicity: the unlink-on-free path (and its lock) disappears
// entirely once the sweep owns freeing, and the list splits per-fiber in the
// stage that closes the cross-fiber escape routes.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <mobius/mobius.h>   // MOBIUS_API

class Value;

enum GcObjectType : uint8_t {
    GC_TABLE = 0,
    GC_ARRAY,
    GC_FUNCTION,
    GC_UPVALUE,
};

// Flags layout: bits 0-1 type, bit 2 mark, bit 3 shared-domain (later stages).
struct GcHeader {
    GcHeader* prev = nullptr;
    GcHeader* next = nullptr;
    void*     obj = nullptr;   // owning object (avoids offsetof on vptr types)
    void*     owner = nullptr; // registry segment that links this header
    uint32_t  flags = 0;

    GcObjectType type() const { return (GcObjectType)(flags & 0x3); }
    bool marked() const { return (flags & 0x4) != 0; }
    void setMarked(bool m) { if (m) flags |= 0x4; else flags &= ~0x4u; }
};

// Link/unlink an object into its thread's registry segment. Lock-free on the
// owning thread; a free from a foreign thread defers the unlink to the owner
// via a pending queue. Called from the tracked types' ctors/dtors.
void gc_track(GcHeader* h, GcObjectType type, void* obj);
void gc_untrack(GcHeader* h);

// Pool-backed allocation for the traced types (fixed-size chunks, per-thread
// free lists, slab-backed). The types' class operator new/delete route
// through these; sz must be the class size (subclasses fall back to the
// global allocator in the class operators). Returns nullptr on OOM.
void* gc_object_alloc(GcObjectType type, size_t sz);
void  gc_object_free(GcObjectType type, void* p);

// Number of currently tracked objects (all types).
size_t gc_tracked_count();

// Invoke `cb(child, ud)` for every TRACED object directly referenced by `h`
// (table entries + metatable, array elements, closure upvalues, an upvalue's
// closed value). Refcounted children are not reported — they are not traced.
typedef void (*GcVisitFn)(GcHeader* child, void* ud);
void gc_traverse_children(GcHeader* h, GcVisitFn cb, void* ud);

// Extract the traced object(s) a Value leads to, looking THROUGH the
// refcounted pass-through holders: an array slice reports its owner array, a
// shared cell reports what its inner value leads to, a channel its buffered
// values, a future its result.
void gc_visit_value_children(const Value& v, GcVisitFn cb, void* ud);

// Walk every tracked object under the registry lock (shadow verification).
void gc_for_each_tracked(GcVisitFn cb, void* ud);

// ---------------------------------------------------------------------------
// Shadow verification (stage 2). Enabled with MOBIUS_GC_SHADOW=1 (report) or
// =2 (report + abort). At quiescent bytecode boundaries, enumerate all roots,
// mark, and check that every refcount-live tracked object was reached: any
// violation is a missed GC root, found while refcounting still guarantees
// correctness.
// ---------------------------------------------------------------------------
class MobiusVM;
extern int g_gc_shadow_mode;   // 0 off, 1 report, 2 report+abort
void gc_shadow_init_from_env();
void gc_shadow_verify_now(MobiusVM* vm);     // unconditional (test hook)

// The collector. gc_safepoint() is the cheap per-hook entry (gates on
// quiescence and native depth, collects under allocation pressure, runs
// shadow verification when enabled). gc_collect() marks from roots and
// frees everything unreachable; returns the number of objects freed.
extern volatile bool g_gc_pending;

// True while the collector's sweep is destroying dead objects. Refcount
// paths that would delete a GC-tracked object at count zero must stand down
// during the sweep: every object that can reach zero then is already in the
// sweep's dead list (unreachable ⇒ unmarked), and the sweep owns freeing it.
// Exported as a function so the flag itself stays private to the collector
// and out of the plugin ABI (only cold count-hit-zero paths call it).
MOBIUS_API bool gc_is_sweeping();
void gc_safepoint(MobiusVM* vm);
size_t gc_collect(MobiusVM* vm);
size_t gc_collect_all_for_teardown();

#endif // MOBIUS_GC_H
