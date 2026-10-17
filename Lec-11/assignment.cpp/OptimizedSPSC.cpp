#include <iostream>
#include <atomic>
#include <cstddef>
#include <new>

template <typename T>
class OptimizedSPSC {
public:
    OptimizedSPSC() = default;

    OptimizedSPSC(size_t size) : mSize(size) {
        mData = static_cast<T *>(::operator new(sizeof(T) * size));
    }

    OptimizedSPSC(const OptimizedSPSC<T> &) = delete;
    OptimizedSPSC(OptimizedSPSC &&) = delete;
    OptimizedSPSC &operator=(const OptimizedSPSC<T> &) = delete;

    ~OptimizedSPSC() {
        ::operator delete(mData);
    }

    // 1. Pass by const reference (no 64-byte stack copy)
    bool push(const T& val) {
        size_t currentPush = mPushIdx.load(std::memory_order_relaxed);

        // 2. Read mPopIdx ONLY when the buffer appears full relative to mCachedPopIdx
        if (currentPush - mCachedPopIdx == mSize) [[unlikely]] {
            mCachedPopIdx = mPopIdx.load(std::memory_order_acquire);
            if (currentPush - mCachedPopIdx == mSize) [[unlikely]] {
                return false;
            }
        }

        mData[currentPush & (mSize - 1)] = val;

        // 3. Simple release store replaces atomic fetch_add (RMW instruction)
        mPushIdx.store(currentPush + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& val) {
        size_t currentPop = mPopIdx.load(std::memory_order_relaxed);

        // 2. Read mPushIdx ONLY when the buffer appears empty relative to mCachedPushIdx
        if (mCachedPushIdx == currentPop) [[unlikely]] {
            mCachedPushIdx = mPushIdx.load(std::memory_order_acquire);
            if (mCachedPushIdx == currentPop) [[unlikely]] {
                return false;
            }
        }

        val = mData[currentPop & (mSize - 1)];
        mData[currentPop & (mSize - 1)].~T();

        // 3. Simple release store replaces atomic fetch_add (RMW instruction)
        mPopIdx.store(currentPop + 1, std::memory_order_release);
        return true;
    }

private:
    T* mData{nullptr};
    size_t mSize{0};

    // Isolated Cache Lines (64 bytes each) to eliminate false sharing & cache bouncing
    alignas(64) std::atomic<size_t> mPushIdx{0u};
    alignas(64) size_t mCachedPopIdx{0u};

    alignas(64) std::atomic<size_t> mPopIdx{0u};
    alignas(64) size_t mCachedPushIdx{0u};
};
