// SPSC queue with std::mutex
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

// 64 bytes = 1 cache line
struct Order {
  long long id;
  char data[56];
};
static_assert(sizeof(Order) == 64, "order should be 64 bytes");

// ring buffer, size should be power of 2
template <typename T>
class SPSC {
public:
  SPSC(size_t size) : mSize(size) {
    // memory pool, allocate only once
    mData = static_cast<T*>(::operator new(sizeof(T) * size));
  }
  SPSC(const SPSC<T>&) = delete;
  SPSC(SPSC&&) = delete;
  SPSC& operator=(const SPSC<T>&) = delete;
  ~SPSC() {
    ::operator delete(mData);
  }

  bool push(const T& val) {
    mLock.lock();
    if (size() == mSize) [[unlikely]] { // full
      mLock.unlock();
      return false;
    }
    mData[mPushIdx & (mSize - 1)] = val; // & instead of %
    mPushIdx++;
    mLock.unlock();
    return true;
  }

  // no extra copy
  bool pop(T& val) {
    mLock.lock();
    if (empty()) [[unlikely]] { // empty
      mLock.unlock();
      return false;
    }
    val = mData[mPopIdx & (mSize - 1)];
    mData[mPopIdx & (mSize - 1)].~T(); // destroy old obj
    mPopIdx++;
    mLock.unlock();
    return true;
  }

private:
  size_t size() { return mPushIdx - mPopIdx; }
  bool empty() { return mPushIdx == mPopIdx; }

  T* mData{nullptr};
  size_t mSize{0u};
  size_t mPushIdx{0u};
  size_t mPopIdx{0u};
  std::mutex mLock; // mutex instead of spinlock
};

int main() {
  SPSC<Order> q(1024);
  std::atomic<bool> stop{false};
  // diff cache lines, no false sharing
  alignas(64) long long pushed = 0;
  alignas(64) long long popped = 0;
  bool orderOk = true;

  // producer
  std::thread t1([&] {
    Order o{};
    while (!stop.load()) {
      if (q.push(o)) {
        pushed++;
        o.id++;
      }
    }
  });

  // consumer
  std::thread t2([&] {
    Order o{};
    long long expected = 0;
    while (!stop.load()) {
      if (q.pop(o)) {
        if (o.id != expected) orderOk = false;
        expected++;
        popped++;
      }
    }
  });

  // run for 1 sec
  std::this_thread::sleep_for(std::chrono::seconds(1));
  stop.store(true);
  t1.join();
  t2.join();

  std::cout << "in 1 sec pushed: " << pushed << ", popped: " << popped << "\n";
  if (orderOk)
    std::cout << "order ok\n";
  else
    std::cout << "ORDER WRONG\n";
  return 0;
}
