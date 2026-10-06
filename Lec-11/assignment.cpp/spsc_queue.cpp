#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstddef>
#include <new>

// 64-byte payload to simulate real trading engine data structures
struct alignas(64) Frame {
    uint64_t id;
    uint64_t timestamp;
    char data[48];
};

template <typename T>
class SPSC {
public:
  SPSC() = default;
  
  SPSC(size_t size) : mSize(size) {
    // Raw uninitialized memory allocation (no constructor calls upfront)
    mData = static_cast<T *>(::operator new(sizeof(T) * size));
  }

  SPSC(const SPSC<T> &) = delete;
  SPSC(SPSC &&) = delete;
  SPSC &operator=(const SPSC<T> &) = delete; 

  ~SPSC() { 
    // Free raw memory without calling delete[]
    ::operator delete(mData); 
  }

  bool push(const T& val) {
    // 1. Pipeline branch prediction hint
    if (size() == mSize) [[unlikely]]
      return false;

    // 2. Bitwise AND mask replacing modulo (% mSize)
    mData[mPushIdx & (mSize - 1)] = val;

    // 3. Release store guarantees buffer update is committed before cursor increments
    mPushIdx.fetch_add(1, std::memory_order_release);
    return true;
  }

  bool pop(T& val) {
    // 4. Acquire load ensures we see fresh updates written before producer's release
    if (mPushIdx.load(std::memory_order_acquire) == mPopIdx.load(std::memory_order_relaxed)) [[unlikely]]
      return false;

    val = mData[mPopIdx & (mSize - 1)];

    // Explicit destructor call for raw buffer management
    mData[mPopIdx & (mSize - 1)].~T();

    // Advance consumer index
    mPopIdx.fetch_add(1, std::memory_order_release);
    return true;
  }

private:
  size_t size() const { 
    return mPushIdx.load(std::memory_order_relaxed) - mPopIdx.load(std::memory_order_relaxed); 
  }

  bool empty() const { 
    return mPushIdx.load(std::memory_order_relaxed) == mPopIdx.load(std::memory_order_relaxed); 
  }

  T* mData{nullptr};
  size_t mSize{0};

  // 64-byte alignment isolates producer and consumer writes to separate cache lines
  alignas(64) std::atomic<size_t> mPushIdx{0u};
  alignas(64) std::atomic<size_t> mPopIdx{0u};
};
