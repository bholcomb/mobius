#include "data/channel.h"

// A value crossing a channel crosses a fiber boundary, so it follows the same
// rule as spawn arguments: non-shared arrays/tables/buffers are DEEP-COPIED
// (each side owns an isolated value); `shared var` values travel as their
// SharedCell and keep identity. Before this, a table sent through a channel
// was received by pointer and mutated concurrently with no synchronization at
// all — container-level locking was designed for that hole but never wired up,
// and copy semantics close it without a mutex in every container.
static Value channel_transfer_copy(const Value& val) {
    if ((val.type == VAL_ARRAY && val.as.array) ||
        (val.type == VAL_TABLE && val.as.table) ||
        (val.type == VAL_BUFFER && val.as.buffer) ||
        (val.type == VAL_FUNCTION && val.as.function)) {
        // Closures snapshot their captured upvalues in the copy; a function
        // with no captures is passed through by the deep copy unchanged.
        if ((val.flags & VAL_FLAG_SHARED) == 0)
            return deep_copy_value_for_spawn(val);
    }
    return val;
}

Channel::Channel(size_t capacity)
    : capacity_(capacity > 0 ? capacity : 1)
{
    buffer_.resize(capacity_);
}

Channel::~Channel() {
}

bool Channel::send(const Value& val) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this]() { return count_ < capacity_ || closed_; });
    if (closed_) return false;
    buffer_[tail_] = channel_transfer_copy(val);
    tail_ = (tail_ + 1) % capacity_;
    count_++;
    not_empty_.notify_one();
    return true;
}

bool Channel::recv(Value& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this]() { return count_ > 0 || closed_; });
    if (count_ == 0) return false;
    out = std::move(buffer_[head_]);
    buffer_[head_] = Value();
    head_ = (head_ + 1) % capacity_;
    count_--;
    not_full_.notify_one();
    return true;
}

bool Channel::trySend(const Value& val) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || count_ >= capacity_) return false;
    buffer_[tail_] = channel_transfer_copy(val);
    tail_ = (tail_ + 1) % capacity_;
    count_++;
    not_empty_.notify_one();
    return true;
}

bool Channel::tryRecv(Value& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == 0) return false;
    out = std::move(buffer_[head_]);
    buffer_[head_] = Value();
    head_ = (head_ + 1) % capacity_;
    count_--;
    not_full_.notify_one();
    return true;
}

void Channel::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    not_full_.notify_all();
    not_empty_.notify_all();
}

size_t Channel::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

void Channel::forEachBuffered(void (*cb)(const Value&, void*), void* ud) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < count_; i++) {
        cb(buffer_[(head_ + i) % capacity_], ud);
    }
}
