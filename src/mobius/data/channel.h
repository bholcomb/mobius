#ifndef MOBIUS_DATA_CHANNEL_H
#define MOBIUS_DATA_CHANNEL_H

#include "data/value.h"
#include "internal/ref_counted.h"

#include <mutex>
#include <condition_variable>
#include <vector>

class Channel : public RefCounted {
public:
    explicit Channel(size_t capacity);
    ~Channel() override;

    bool send(const Value& val);
    bool recv(Value& out);

    bool trySend(const Value& val);
    bool tryRecv(Value& out);

    void close();
    bool isClosed() const { return closed_; }

    // Visit buffered (queued, not yet received) values under the lock — used
    // by the GC to reach values in transit between fibers.
    void forEachBuffered(void (*cb)(const Value&, void*), void* ud);

    size_t capacity() const { return capacity_; }
    size_t size() const;

private:
    std::vector<Value> buffer_;
    size_t capacity_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    bool closed_ = false;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};

#endif // MOBIUS_DATA_CHANNEL_H
