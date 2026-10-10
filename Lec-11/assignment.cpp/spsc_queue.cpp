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

#include <array>
#include <mutex>
#include <cstddef>
#include "spin_lock.cpp"

template<typename T, size_t Size>
class SpscQueue {
private:
    alignas(64) size_t mPopIdx{0};
    alignas(64) size_t mPushIdx{0};
    std::array<T, Size> mStorage;
    SpinLock mLock;

public:
    bool push(const T& item) {
        std::lock_guard<SpinLock> guard(mLock);
        if (mPushIdx - mPopIdx < Size) {
            mStorage[mPushIdx % Size] = item;
            mPushIdx++;
            return true;
        }
        return false;
    }

    bool pop(T& item) {
        std::lock_guard<SpinLock> guard(mLock);
        if (mPushIdx > mPopIdx) {
            item = mStorage[mPopIdx % Size];
            mPopIdx++;
            return true;
        }
        return false;
    }
};