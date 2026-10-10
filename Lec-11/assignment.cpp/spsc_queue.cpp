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
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

struct Obj {
    char arr[64];
};
class SPSC {
private:
    Obj* mData{nullptr};
    uint mSize{0u};
    uint mPushIdx{0u};
    uint mPopIdx{0u};
    std::mutex mLock;

public:
    SPSC(uint size) {
        mSize = size;
        mData = new Obj[size];
    }

    ~SPSC() {
        delete[] mData;
    }

    bool push(const Obj &val) {
        mLock.lock();
        if(mPushIdx - mPopIdx == mSize) {
            //queue is full
            mLock.unlock();
            return false;
        }
        mData[mPushIdx % mSize] = val;
        mPushIdx++;
        mLock.unlock();
        return true;
    }

    bool pop(Obj &val) {
        mLock.lock();
        if(mPopIdx == mPushIdx) {
            //queue is empty
            mLock.unlock();
            return false;
        }
        val = mData[mPopIdx % mSize];
        mPopIdx++;
        mLock.unlock();
        return true;
    }
};

std::atomic<bool> active{true};
std::atomic<long long> pushed = 0;
std::atomic<long long> popped = 0;

void producer(SPSC &q) {
    Obj o{};
    char id = 0;

    while(active) {
        for (int i = 0; i < 64; i++) o.arr[i] = (char)(id + i);

        if(q.push(o)) pushed++;
    }
}

void consumer(SPSC &q) {
    Obj o{};
    while(active) {
        if(q.pop(o)) popped++;
    }
}

int main() {
    SPSC q(1000);
    std::thread t1(producer, std::ref(q));
    std::thread t2(consumer, std::ref(q));

    std::this_thread::sleep_for(std::chrono::seconds(1));

    active = false;

    t1.join(); t2.join();

    std::cout << pushed << " pushed per second" << std::endl;
    std::cout << popped << " popped per second" << std::endl;
}