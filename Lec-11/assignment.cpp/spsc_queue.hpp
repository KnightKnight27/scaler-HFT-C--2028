#pragma once
#include <atomic>
#include <cstdint>
#include <array>
#include <mutex>
#include <thread>

using namespace std;

struct Object64 {
    array<uint64_t, 8> value{};
};

static_assert(sizeof(Object64) == 64);

class SpinLock {
public:
    void lock() {
        while (flag_.test_and_set(memory_order_acquire)) {
        }
    }
    void unlock() { flag_.clear(memory_order_release); }

private:
    atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

template <size_t cap, typename Lock>
class SPSCQ {
private:
    array<Object64, cap> buffer_;
    atomic<size_t> head_{0};
    atomic<size_t> tail_{0};
    atomic<size_t> size_{0};
    Lock lock_;
public:
    bool push(const Object64& obj) {
        lock_guard<Lock> lock(lock_);
        if(size_ == cap) return false;

        buffer_[tail_] = obj;
        tail_ = (tail_ + 1) % cap;
        size_++;

        return true;
    }

    bool pop(Object64& obj) {
        lock_guard<Lock> lock(lock_);
        if(size_ == 0) return false;

        obj = buffer_[head_];
        head_ = (head_ + 1) % cap;
        size_--;

        return true;
    }
};

template <typename Lock>
void test_spsc_queue() {
    SPSCQ<1024, Lock> queue;
    atomic<bool> done{false};
    uint64_t push_count = 0, pop_count = 0;

    thread maker([&]() {
        Object64 obj{};
        while(!done.load(memory_order_relaxed)) {
            obj.value[0]++;
            if(queue.push(obj)) {
                push_count++;
            }
        }
    });

    thread eater([&]() {
        Object64 obj{};
        while(!done.load(memory_order_relaxed)) {
            if(queue.pop(obj)) {
                pop_count++;
            }
        }
    });

    this_thread::sleep_for(chrono::seconds(1));
    done.store(true, memory_order_relaxed);

    maker.join();
    eater.join();

    cout << "Push count: " << push_count << endl;
    cout << "Pop count: " << pop_count << endl;
}
