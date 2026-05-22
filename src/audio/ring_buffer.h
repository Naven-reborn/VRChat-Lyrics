#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace audio {

// SPSC byte ring buffer. Single producer (capture thread), single consumer
// (render thread); both currently on the same worker thread, but we use
// atomics so we can split them later without rewriting. Size is rounded up
// to a power of two so the wrap is a cheap mask.
class RingBuffer {
public:
    void Init(size_t bytes) {
        size_t n = 1;
        while (n < bytes) n <<= 1;
        buf_.assign(n, 0);
        mask_ = n - 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    void Reset() {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    size_t Capacity() const { return buf_.size(); }

    size_t Available() const {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        return h - t;
    }

    size_t Free() const { return Capacity() - Available(); }

    // Write up to `bytes` from src. If there's not enough free space, the
    // oldest data is dropped (tail advanced) so the producer never blocks.
    // Returns bytes written (== bytes unless Capacity() < bytes).
    size_t Write(const void* src, size_t bytes) {
        if (bytes == 0 || buf_.empty()) return 0;
        if (bytes > Capacity()) bytes = Capacity();

        size_t free = Free();
        if (bytes > free) {
            size_t drop = bytes - free;
            tail_.fetch_add(drop, std::memory_order_release);
        }

        size_t h = head_.load(std::memory_order_relaxed);
        size_t cap = buf_.size();
        size_t off = h & mask_;
        size_t first = bytes < cap - off ? bytes : cap - off;
        std::memcpy(buf_.data() + off, src, first);
        if (bytes > first) {
            std::memcpy(buf_.data(), (const uint8_t*)src + first, bytes - first);
        }
        head_.store(h + bytes, std::memory_order_release);
        return bytes;
    }

    // Read up to `bytes` into dst. Returns bytes actually copied.
    size_t Read(void* dst, size_t bytes) {
        if (bytes == 0 || buf_.empty()) return 0;
        size_t avail = Available();
        if (bytes > avail) bytes = avail;
        if (bytes == 0) return 0;

        size_t t = tail_.load(std::memory_order_relaxed);
        size_t cap = buf_.size();
        size_t off = t & mask_;
        size_t first = bytes < cap - off ? bytes : cap - off;
        std::memcpy(dst, buf_.data() + off, first);
        if (bytes > first) {
            std::memcpy((uint8_t*)dst + first, buf_.data(), bytes - first);
        }
        tail_.store(t + bytes, std::memory_order_release);
        return bytes;
    }

private:
    std::vector<uint8_t> buf_;
    size_t               mask_ = 0;
    std::atomic<size_t>  head_{0}; // bytes ever written
    std::atomic<size_t>  tail_{0}; // bytes ever read
};

} // namespace audio
