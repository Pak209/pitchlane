#pragma once
// Lock-free single-producer / single-consumer ring buffer and a seqlock snapshot.
// The producer (audio thread) never blocks, allocates or waits.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace pitchlane {

template <typename T, size_t Capacity>
class SpscRing
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");

public:
    /** Producer side. Returns false (and drops the item) if full. */
    bool push(const T& item) noexcept
    {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        if (w - r >= Capacity) return false;
        buf_[w & (Capacity - 1)] = item;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }

    /** Consumer side. */
    bool pop(T& item) noexcept
    {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        if (r == w) return false;
        item = buf_[r & (Capacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

    size_t size() const noexcept
    {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> buf_ {};
    alignas(64) std::atomic<size_t> write_ { 0 };
    alignas(64) std::atomic<size_t> read_ { 0 };
};

/** Single-writer seqlock: the writer never blocks; readers retry if they raced a write. */
template <typename T>
class SeqLockValue
{
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");

public:
    void store(const T& v) noexcept
    {
        const unsigned s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        copyIn(v);
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(s + 2, std::memory_order_release);
    }

    T load() const noexcept
    {
        T out {};
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const unsigned s1 = seq_.load(std::memory_order_acquire);
            if (s1 & 1u) continue;
            copyOut(out);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == s1) break;
        }
        return out;
    }

private:
    // Word-wise relaxed atomic copies keep this free of formal data races.
    static constexpr size_t kWords = (sizeof(T) + sizeof(unsigned) - 1) / sizeof(unsigned);

    void copyIn(const T& v) noexcept
    {
        std::array<unsigned, kWords> tmp {};
        std::memcpy(tmp.data(), static_cast<const void*>(&v), sizeof(T));
        for (size_t i = 0; i < kWords; ++i) words_[i].store(tmp[i], std::memory_order_relaxed);
    }

    void copyOut(T& v) const noexcept
    {
        std::array<unsigned, kWords> tmp {};
        for (size_t i = 0; i < kWords; ++i) tmp[i] = words_[i].load(std::memory_order_relaxed);
        std::memcpy(static_cast<void*>(&v), tmp.data(), sizeof(T));
    }

    std::atomic<unsigned> seq_ { 0 };
    std::array<std::atomic<unsigned>, kWords> words_ {};
};

} // namespace pitchlane
