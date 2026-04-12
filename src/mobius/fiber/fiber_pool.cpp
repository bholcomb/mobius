#include "fiber/fiber_pool.h"

#include <cstdlib>
#include <cstdio>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

static size_t get_page_size() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return (size_t)sysconf(_SC_PAGESIZE);
#endif
}

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Allocate stack memory with a guard page at the bottom to catch overflow.
// Layout: [guard page | usable stack]
// Stack grows downward, so the guard page is at the low address.
static void* alloc_stack(size_t usable_size, size_t page_size) {
    size_t total = page_size + usable_size;
#ifdef _WIN32
    void* mem = VirtualAlloc(nullptr, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) return nullptr;
    DWORD old;
    VirtualProtect(mem, page_size, PAGE_NOACCESS, &old);
#else
    void* mem = mmap(nullptr, total, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return nullptr;
    mprotect(mem, page_size, PROT_NONE);
#endif
    return mem;
}

static void free_stack(void* mem, size_t usable_size, size_t page_size) {
    size_t total = page_size + usable_size;
#ifdef _WIN32
    VirtualFree(mem, 0, MEM_RELEASE);
    (void)total;
#else
    munmap(mem, total);
#endif
}

FiberPool::FiberPool(size_t fiber_stack_size, size_t initial_count, size_t max_count)
    : fiber_stack_size_(align_up(fiber_stack_size, get_page_size())),
      max_count_(max_count), next_id_(1) {
    all_fibers_.reserve(initial_count);
    free_list_.reserve(initial_count);
    for (size_t i = 0; i < initial_count; i++) {
        MobiusFiber* f = allocateFiber();
        if (f) free_list_.push_back(f);
    }
}

FiberPool::~FiberPool() {
    for (MobiusFiber* f : all_fibers_) {
        deallocateFiber(f);
    }
}

MobiusFiber* FiberPool::allocateFiber() {
    size_t page_size = get_page_size();
    void* mem = alloc_stack(fiber_stack_size_, page_size);
    if (!mem) return nullptr;

    MobiusFiber* fiber = new MobiusFiber();
    fiber->id = next_id_++;
    fiber->stack_memory = mem;
    fiber->stack_size = fiber_stack_size_;
    fiber->state = FiberState::Idle;
    fiber->vm = nullptr;
    fiber->cancel_requested.store(false, std::memory_order_relaxed);
    fiber->peak_stack_bytes = 0;

    all_fibers_.push_back(fiber);
    return fiber;
}

void FiberPool::deallocateFiber(MobiusFiber* fiber) {
    if (fiber->stack_memory) {
        free_stack(fiber->stack_memory, fiber->stack_size, get_page_size());
    }
    delete fiber;
}

MobiusFiber* FiberPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!free_list_.empty()) {
        MobiusFiber* f = free_list_.back();
        free_list_.pop_back();
        f->state = FiberState::Idle;
        f->cancel_requested.store(false, std::memory_order_relaxed);
        f->peak_stack_bytes = 0;
        f->vm = nullptr;
        return f;
    }

    if (all_fibers_.size() >= max_count_) {
        return nullptr;
    }

    // Grow: allocate a new fiber on demand
    return allocateFiber();
}

void FiberPool::release(MobiusFiber* fiber) {
    std::lock_guard<std::mutex> lock(mutex_);
    fiber->state = FiberState::Idle;
    fiber->vm = nullptr;
    free_list_.push_back(fiber);
}

size_t FiberPool::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // active = total - free
    return all_fibers_.size() - free_list_.size();
}

size_t FiberPool::totalCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_fibers_.size();
}
