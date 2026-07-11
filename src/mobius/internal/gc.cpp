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
