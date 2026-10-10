#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

struct Msg {
  uint64_t id;
  uint64_t price;
  uint64_t qty;
  uint64_t ts;
  uint64_t pad[4];
};

static_assert(sizeof(Msg) == 64);

class SpinLock {
 public:
  void lock() { while (mFlag.exchange(true, std::memory_order_acquire)) {} }
  void unlock() { mFlag.store(false, std::memory_order_release); }

 private:
  std::atomic<bool> mFlag{false};
};

template <typename T>
class SPSC {
 public:
  SPSC(size_t size) : mSize(size), mMask(size - 1), mData(new T[size]) {
    assert((size & (size - 1)) == 0);
  }
  ~SPSC() { delete[] mData; }

  SPSC(const SPSC&) = delete;
  SPSC& operator=(const SPSC&) = delete;

  bool push(const T& v) {
    mLock.lock();
    if (mPushIdx - mPopIdx == mSize) {
      mLock.unlock();
      return false;
    }
    mData[mPushIdx & mMask] = v;
    mPushIdx++;
    mLock.unlock();
    return true;
  }

  bool pop(T& out) {
    mLock.lock();
    if (mPopIdx == mPushIdx) {
      mLock.unlock();
      return false;
    }
    out = mData[mPopIdx & mMask];
    mPopIdx++;
    mLock.unlock();
    return true;
  }

 private:
  SpinLock mLock;
  size_t mSize;
  size_t mMask;
  size_t mPushIdx{0};
  size_t mPopIdx{0};
  T* mData{nullptr};
};