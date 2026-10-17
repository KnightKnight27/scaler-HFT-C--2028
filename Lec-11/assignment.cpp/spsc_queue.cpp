// WRITE AN SPSC QUEUE 
// SPINLOCK ( WHILE LOOP) OR STD::MUTEX 
// t1.join()  t2.join()
// producer consumer to push objects and pop objects 
//
// you need to figure out a way that with locks how many 
// 64 byte objects can u push and pop in 1 second
//  raise a git PR for the same 
//  add readme for ur per second specs 
//  feel free to add worst code qaulity :)
//
//
// ^^ MEMORY POOL ^^


#include <iostream>
#include <thread>
#include <atomic>
#include <new>
#include <chrono>
#include <optional>
#include <cassert>

template <typename T>
class SPSC;

template <typename T>

class PopProxy {
    SPSC<T>* queue;
    T* ptr;
    size_t index;

public:
    PopProxy(SPSC<T>* q, T* p, size_t i)
        : queue(q), ptr(p), index(i) {}

    PopProxy(const PopProxy&) = delete;
    PopProxy& operator=(const PopProxy&) = delete;

    PopProxy(PopProxy&& other) noexcept
        : queue(other.queue),
          ptr(other.ptr),
          index(other.index) {
        other.queue = nullptr;
        other.ptr = nullptr;
    }

    PopProxy& operator=(PopProxy&& other) noexcept {
        if (this != &other) {
            if (queue != nullptr) {
                queue->finishPop(index);
            }

            queue = other.queue;
            ptr = other.ptr;
            index = other.index;

            other.queue = nullptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    T* operator->() const noexcept {
        return ptr;
    }

    ~PopProxy() {
        if (queue != nullptr) {
            queue->finishPop(index);
        }
    }
};

template <typename T>
class SPSC {
public:
    SPSC() = default;

    explicit SPSC(size_t size) : mSize(size) {
        assert(size > 0 && (size & (size - 1)) == 0);
        mData = static_cast<T*>(::operator new(sizeof(T) * size));
    }

    SPSC(const SPSC&) = delete;
    SPSC(SPSC&&) = delete;
    SPSC& operator=(const SPSC&) = delete;
    SPSC& operator=(SPSC&&) = delete;

    ~SPSC() {
        ::operator delete(mData);
    }

bool push(const T& val) {
    size_t pushIdx = mPushIdx.load(std::memory_order_relaxed);

    if (pushIdx - mCachedPopIdx == mSize) [[unlikely]] {
        mCachedPopIdx = mPopIdx.load(std::memory_order_acquire);

        if (pushIdx - mCachedPopIdx == mSize) {
            return false;
        }
    }

    size_t index = pushIdx & (mSize - 1);
    new (&mData[index]) T(val);

    mPushIdx.store(pushIdx + 1, std::memory_order_release);
    return true;
}
std::optional<PopProxy<T>> pop() {
    size_t popIdx = mPopIdx.load(std::memory_order_relaxed);

    if (popIdx == mCachedPushIdx) [[unlikely]] {
        mCachedPushIdx = mPushIdx.load(std::memory_order_acquire);

        if (popIdx == mCachedPushIdx) {
            return std::nullopt;
        }
    }

    size_t index = popIdx & (mSize - 1);
    return PopProxy<T>(this, &mData[index], index);
}
private:
    template <typename>
    friend class PopProxy;

    T* mData = nullptr;
    size_t mSize = 0;
    size_t mCachedPopIdx = 0; 
    size_t mCachedPushIdx = 0;  
    alignas(64) std::atomic<size_t> mPushIdx{0};
    alignas(64) std::atomic<size_t> mPopIdx{0};

    void finishPop(size_t index) {
        mData[index].~T();
        mPopIdx.fetch_add(1, std::memory_order_release);
    }
};

struct Object {
    int id;
    char data[60];
    // for making the size of Object 64 bytes
};

SPSC<Object> q(1024);

constexpr size_t iters = 100000000;

void producer() {
    Object obj{};

    for (size_t i = 0; i < iters; i++) {
        obj.id = static_cast<int>(i);

        while (!q.push(obj)) {
        }
    }
}

void consumer() {
    for (size_t i = 0; i < iters; i++) {
        auto item = q.pop();

        while (!item) {
            item = q.pop();
        }

        if ((*item)->id != static_cast<int>(i)) {
            std::cout << "Error: Expected " << i
                      << ", got " << (*item)->id << '\n';
            return;
        }
    }
}

int main() {
    auto start = std::chrono::steady_clock::now();

    std::thread t1(producer);
    std::thread t2(consumer);

    t1.join();
    t2.join();

    auto end = std::chrono::steady_clock::now();

    double seconds =
        std::chrono::duration<double>(end - start).count();

    std::cout << "Time: " << seconds << " seconds\n";
    std::cout << "Ops/sec: " << iters / seconds << '\n';
}
