#ifndef MOBIUS_DATA_SHARED_CELL_H
#define MOBIUS_DATA_SHARED_CELL_H

#include "internal/ref_counted.h"
#include "fiber/fiber_mutex.h"

#include <mutex>

class Value;

class SharedCell : public RefCounted {
public:
    explicit SharedCell(const Value& initial);
    ~SharedCell() override;

    Value load();
    void store(const Value& val);
    Value& unsafeValue() { return *value_; }
    const Value& unsafeValue() const { return *value_; }

    FiberMutex& mutex() { return mutex_; }

private:
    // Take ownership of a container value and share its nested containers.
    void adopt(const Value& v);
    // Give up ownership of the container currently held.
    void release_ownership();

    Value* value_;
    FiberMutex mutex_;
};

// Sharing goes all the way down: an array, table or buffer inside a shared
// value, or stored into one later, is itself shared, with its own cell and
// lock. Before, nested containers were reachable from every fiber but had
// no lock, so concurrent `t.inner:push(x)` corrupted the heap.
//
// Returns the shared form of v: for a container, the cell that owns it (a
// new cell if it has none yet); any other value is returned unchanged.
Value share_for_cell(const Value& v);

#endif // MOBIUS_DATA_SHARED_CELL_H
