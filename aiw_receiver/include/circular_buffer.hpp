// Lock-free single-producer / single-consumer ring buffer.
//
// Slots are pre-allocated; the producer fills a slot in place via
// `write_slot()` + `commit_write()` and the consumer drains it via
// `read_slot()` + `commit_read()`, so large chunk structs are never copied.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <new>
#include <thread>

namespace aiw {

// Fixed rather than std::hardware_destructive_interference_size, whose value
// is ABI-unstable across -march settings.
inline constexpr std::size_t CACHE_LINE = 64;

template <typename T>
class SpscRing {
public:
    explicit SpscRing(std::size_t capacity_pow2)
        : cap_(round_up_pow2(capacity_pow2)), mask_(cap_ - 1),
          slots_(std::make_unique<T[]>(cap_)) {}

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    std::size_t capacity() const { return cap_; }

    // ---- producer side ----
    T* write_slot() {
        const auto head = head_.load(std::memory_order_relaxed);
        if (head - tail_cache_ == cap_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (head - tail_cache_ == cap_) return nullptr;
        }
        return &slots_[head & mask_];
    }
    void commit_write() { head_.store(head_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    bool try_push(const T& v) {
        T* s = write_slot();
        if (!s) return false;
        *s = v;
        commit_write();
        return true;
    }

    // ---- consumer side ----
    T* read_slot() {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (tail == head_cache_) return nullptr;
        }
        return &slots_[tail & mask_];
    }
    void commit_read() { tail_.store(tail_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    bool try_pop(T& out) {
        T* s = read_slot();
        if (!s) return false;
        out = *s;
        commit_read();
        return true;
    }

    std::size_t size_approx() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

private:
    static std::size_t round_up_pow2(std::size_t v) {
        std::size_t p = 2;
        while (p < v) p <<= 1;
        return p;
    }

    const std::size_t cap_;
    const std::size_t mask_;
    std::unique_ptr<T[]> slots_;

    alignas(CACHE_LINE) std::atomic<std::size_t> head_{0};
    std::size_t tail_cache_ = 0;   // producer-local
    alignas(CACHE_LINE) std::atomic<std::size_t> tail_{0};
    std::size_t head_cache_ = 0;   // consumer-local
};

// Spin briefly, then yield, then sleep: keeps latency low without burning a
// core when the pipeline is idle.
class Backoff {
public:
    void pause() {
        if (n_ < 64) {
            ++n_;
        } else if (n_ < 128) {
            ++n_;
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    void reset() { n_ = 0; }

private:
    int n_ = 0;
};

}  // namespace aiw
