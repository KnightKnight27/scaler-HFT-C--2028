#include <iostream>
#include <cstddef>
#include <atomic>
#include <new>

template <typename T>
class SpinLockSPSC{
  public:
    SpinLockSPSC()= default;
    SpinLockSPSC(size_t size): mSize(size){
      //raw mem allocationT
      mData = static_cast<T *>(:: operator new (sizeof(T) * size));
    }
    SpinLockSPSC(const SpinLockSPSC<T> &)= delete;
    SpinLockSPSC(SpinLockSPSC &&) = delete;
    SpinLockSPSC &operator = (const SpinLockSPSC<T> &)= delete;

    ~SpinLockSPSC(){
      ::operator delete(mData);
    }
    //spin until we acquire the flag
    void lock(){
      while (mFlag.test_and_set(std::memory_order_acquire)){
        #if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause(); // keeps the cpu pipeleine from burning excess power while spinning
        #endif
      }
    }

    // release the flag
    void unlock(){
      mFlag.clear(std::memory_order_release);
    }

    bool push(const T& val){
      lock(); // acquire spinlock

      //check if full
      if(mPushIdx - mPopIdx == mSize)[[unlikely]]{
        unlock();
        return false;
      }

      // insert item using power 2 bitwise mask
      mData[mPushIdx & (mSize -1)] = val;
      mPushIdx++;

      unlock(); // release spinlock
      return true;
    }
    bool pop(T& val){
      lock(); // acquire spinlock

      //check if empty
      if(mPushIdx == mPopIdx)[[unlikely]]{
        unlock();
        return false;
      }

      //Read item and call destructor
      val = mData[mPopIdx & (mSize -1)];
      mData[mPopIdx & (mSize -1)].~T();
      mPopIdx++;

      unlock(); // release spinlock
      return true;
    }

    private:
      T* mData{nullptr};
      size_t mSize{0};

      //plain indices protected by spinlock
      size_t mPushIdx{0};
      size_t mPopIdx{0};

      //raw spinlock flag
      std::atomic_flag mFlag = ATOMIC_FLAG_INIT;
};
