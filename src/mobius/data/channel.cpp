#include "data/channel.h"

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
    buffer_[tail_] = val;
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
    buffer_[tail_] = val;
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
