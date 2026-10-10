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
#include <cstddef>
#include <array>
#include <chrono>
#include <cstdint>

class SpinLock {
    std::atomic_flag locked = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (locked.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() {
        locked.clear(std::memory_order_release);
    }
};

template <typename T>
class SPSC {
public:
    SPSC() = default;

    SPSC(size_t size) : mSize(size) {
        mData = static_cast<T*>(::operator new(sizeof(T) * size));

        for (size_t i = 0; i < size; i++) {
            new (&mData[i]) T{};
        }
    }

    SPSC(const SPSC<T>&) = delete;
    SPSC(SPSC&&) = delete;
    SPSC& operator=(const SPSC<T>&) = delete;
    SPSC& operator=(SPSC&&) = delete;

    ~SPSC() {::operator delete(mData);}

    bool push(const T& val) {
        lock.lock();

        if (size() == mSize) [[unlikely]] {
            lock.unlock();
            return false;
        }

        mData[mPushIdx % mSize] = val;
        mPushIdx++;

        lock.unlock();
        return true;
    }

    bool pop(T& val) {
        lock.lock();

        if (mPushIdx == mPopIdx) [[unlikely]] {
            lock.unlock();
            return false;
        }

        val = mData[mPopIdx % mSize];
        mPopIdx++;

        lock.unlock();
        return true;
    }

private:
    size_t size() {
        return mPushIdx - mPopIdx;
    }

    T* mData{nullptr};
    size_t mSize{0};

    size_t mPushIdx{0};
    size_t mPopIdx{0};

    SpinLock lock;
};

using Obj = std::array<char, 64>;

SPSC<Obj> q(1024);

std::atomic<bool> running{true};
std::uint64_t pushes = 0;
std::uint64_t pops = 0;

void producer() {
    Obj value{};

    while (running.load(std::memory_order_relaxed)) {
        if (q.push(value)) pushes++;
    }
}

void consumer() {
    Obj value{};

    while (running.load(std::memory_order_relaxed)) {
        if (q.pop(value)) pops++;
    }
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);

    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    running = false;

    t1.join();
    t2.join();

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << "duration: " << seconds << " seconds" << std::endl;
    std::cout << "objects pushed: " << pushes << std::endl;
    std::cout << "objects popped: " << pops << std::endl;
    std::cout << "pushes per second: " << pushes / seconds << std::endl;
    std::cout << "pops per second: " << pops / seconds << std::endl;

    return 0;
}
