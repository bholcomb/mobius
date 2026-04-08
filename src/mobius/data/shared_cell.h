#ifndef MOBIUS_DATA_SHARED_CELL_H
#define MOBIUS_DATA_SHARED_CELL_H

#include "internal/ref_counted.h"

#include <mutex>

class Value;

class SharedCell : public RefCounted {
public:
    explicit SharedCell(const Value& initial);
    ~SharedCell() override;

    Value load();
    void store(const Value& val);

    std::recursive_mutex& mutex() { return mutex_; }

private:
    Value* value_;
    std::recursive_mutex mutex_;
};

#endif // MOBIUS_DATA_SHARED_CELL_H
