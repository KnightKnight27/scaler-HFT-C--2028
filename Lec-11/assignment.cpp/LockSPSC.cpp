#include <iostream>
#include <mutex>
#include <new>
#include <cstddef>

template <typename T>
class LockSPSC {
  public:
    LockSPSC() = default;
    LockSPSC(size_t size): mSize(size){
      // raw uninitialized memory allocation 
      mData = static_cast<T *>(::operator new(sizeof(T) * size));
    }
    LockSPSC(const LockSPSC<T> &) = delete;
    LockSPSC(LockSPSC && )= delete;
    LockSPSC &operator = (const LockSPSC<T> &) = delete;

    ~LockSPSC(){
      ::operator delete(mData);
    }

    bool push(const T& val){
      // lock the whole queue so only 1 thread touches the memory at a time
      std::lock_guard<std::mutex> lock(mMutex);
      //check if q is full
      if(mPushIdx - mPopIdx == mSize) [[unlikely]]
        return false;

      // push element in array slot using power of 2 bitwise map instead of overhead %
      // cause % causes 20-30 cpu cycles.
      mData[mPushIdx & (mSize -1)] = val;

      // no atomics required cause lock is protecting it
      mPushIdx++;
      return true;
    }

    bool pop(T& val){
      // same as push lock the q first of all
      std::lock_guard<std::mutex> lock(mMutex);

      //check if q is empty
      if(mPushIdx == mPopIdx)[[unlikely]]
        return false;

      // read element at popIdx 
      val = mData[mPopIdx & (mSize -1)];
      //call the destructor manually
      mData[mPopIdx & (mSize -1)].~T();

      //increment popIdx 
      mPopIdx++;
      return true;
    }

    private:
      T* mData{nullptr};
      size_t mSize{0};

      // no atomics needed cause lock is heavy lifting protection over here
      size_t mPushIdx{0};
      size_t mPopIdx{0};

      // the mutex which protects the whole q 
      std::mutex mMutex;
};
