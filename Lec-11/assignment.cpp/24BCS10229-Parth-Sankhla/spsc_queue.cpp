// spsc queue with spinlock / std::mutex, counts 64 byte objects pushed and popped in 1 sec

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <thread>

struct alignas(64) Object {
    uint64_t seq;
    uint64_t payload[7];
};
static_assert(sizeof(Object) == 64, "object should be 64 bytes");

class SpinLock {
public:
    void lock() {
        bool expected = false;
        while (!mLocked.compare_exchange_weak(expected, true, std::memory_order_acquire))
            expected = false;
    }
    void unlock() { mLocked.store(false, std::memory_order_release); }

private:
    std::atomic<bool> mLocked{false};
};

// memory pool, allocated once
template <typename T>
class Pool {
public:
    explicit Pool(size_t size)
        : mData(static_cast<T*>(::operator new(sizeof(T) * size, std::align_val_t(alignof(T))))) {}
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    ~Pool() { ::operator delete(mData, std::align_val_t(alignof(T))); }

    T& operator[](size_t i) { return mData[i]; }

private:
    T* mData{nullptr};
};

template <typename T, typename Lock>
class LockSPSC {
public:
    explicit LockSPSC(size_t size) : mData(size), mSize(size) {}

    bool push(const T& val) {
        std::lock_guard<Lock> g(mLock);
        if (mPushIdx - mPopIdx == mSize) [[unlikely]]
            return false;  // full
        mData[mPushIdx & (mSize - 1)] = val;
        mPushIdx++;
        return true;
    }

    bool pop(T& val) {
        std::lock_guard<Lock> g(mLock);
        if (mPushIdx == mPopIdx) [[unlikely]]
            return false;  // empty
        val = mData[mPopIdx & (mSize - 1)];
        mPopIdx++;
        return true;
    }

private:
    Pool<T> mData;
    size_t mSize;
    size_t mPushIdx{0u};
    size_t mPopIdx{0u};
    Lock mLock;
};

// no lock, just to compare
template <typename T>
class AtomicSPSC {
public:
    explicit AtomicSPSC(size_t size) : mData(size), mSize(size) {}

    bool push(const T& val) {
        size_t push = mPushIdx.load(std::memory_order_relaxed);
        if (push - mPopIdx.load(std::memory_order_acquire) == mSize) [[unlikely]]
            return false;
        mData[push & (mSize - 1)] = val;
        mPushIdx.store(push + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& val) {
        size_t pop = mPopIdx.load(std::memory_order_relaxed);
        if (pop == mPushIdx.load(std::memory_order_acquire)) [[unlikely]]
            return false;
        val = mData[pop & (mSize - 1)];
        mPopIdx.store(pop + 1, std::memory_order_release);
        return true;
    }

private:
    Pool<T> mData;
    size_t mSize;
    alignas(64) std::atomic<size_t> mPushIdx{0u};
    alignas(64) std::atomic<size_t> mPopIdx{0u};
};

template <typename Q>
static void run(const char* name, double seconds) {
    Q q(4096);

    std::atomic<bool> stop{false};
    std::atomic<bool> producerDone{false};
    uint64_t pushed = 0;
    uint64_t popped = 0;
    bool ok = true;

    auto start = std::chrono::steady_clock::now();

    std::thread t1([&] {  // producer
        Object obj{};
        uint64_t seq = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            obj.seq = seq;
            for (uint64_t& p : obj.payload) p = ~seq;
            if (q.push(obj)) seq++;
        }
        pushed = seq;
        producerDone.store(true, std::memory_order_release);
    });

    std::thread t2([&] {  // consumer
        Object obj;
        uint64_t expected = 0;
        for (;;) {
            if (q.pop(obj)) {
                if (obj.seq != expected || obj.payload[6] != ~expected) ok = false;
                expected++;
            } else if (producerDone.load(std::memory_order_acquire)) {
                while (q.pop(obj)) {
                    if (obj.seq != expected || obj.payload[6] != ~expected) ok = false;
                    expected++;
                }
                break;
            }
        }
        popped = expected;
    });

    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    stop.store(true, std::memory_order_relaxed);

    t1.join();
    t2.join();

    double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::printf("%-10s pushed=%llu popped=%llu elapsed=%.3fs  -> %.2f M objects/s  [%s]\n",
                name, static_cast<unsigned long long>(pushed),
                static_cast<unsigned long long>(popped), elapsed, popped / elapsed / 1e6,
                (ok && pushed == popped) ? "fifo ok" : "CORRUPT");
}

int main(int argc, char** argv) {
    double seconds = argc > 1 ? std::atof(argv[1]) : 1.0;
    std::printf("sizeof(Object) = %zu bytes, run time = %.1fs each\n", sizeof(Object), seconds);
    run<LockSPSC<Object, SpinLock>>("spinlock", seconds);
    run<LockSPSC<Object, std::mutex>>("std::mutex", seconds);
    run<AtomicSPSC<Object>>("atomic", seconds);
    return 0;
}
