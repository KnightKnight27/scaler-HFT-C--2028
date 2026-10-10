#include <iostream>
#include <thread>
#include <mutex>
#include <new>
#include <atomic>
#include <chrono>

using ll = long long;  // .. just it looks good , simple code


// this is because to make things look good

// 64 byte object
struct Ansh {
    ll id;
    int proudStudent[14]; // only for you sir
};



template <typename T>
class SPSC {
public:
  SPSC(size_t size) : mSize(size) {

    // not activating the constructor
    mData = static_cast<T*>(::operator new(sizeof(T) * size));
  }
  SPSC(const SPSC<T>&) = delete;
  SPSC(SPSC&&) = delete;
  SPSC& operator=(const SPSC<T>&) = delete;
  ~SPSC() {

     while (mPopIdx < mPushIdx) {
        mData[mPopIdx & (mSize - 1)].~T();
        mPopIdx++;
    }
    ::operator delete(mData);
  }
  // why these extra constructors don't know , because sir did i did // (following sir's code )


  bool push(const T& val) {

    std::lock_guard<std::mutex> guard(mLock);  // used lock_guard  because now i don'y have to unlock it crazy

    // unlikely because of it is unlikely , the kernel or cpu whoever will not choose this one as pripority
    if (size() == mSize) [[unlikely]] {
      return false;
    }

    size_t idx = mPushIdx & (mSize - 1); //  modulo cost heavely , because of division

    // different from sir's code  , because in next line
    // this is called placement new , learned a new thing
    // also we cannot use  = because we deleted the operator =
    new (&mData[idx]) T(val);  // used becuase we are removing the it in pop , so it is better to use this

    mPushIdx++;
    return true;
  }

  bool pop(T& val) {
    std::lock_guard<std::mutex> guard(mLock);


    if (empty()) [[unlikely]] {
      return false;
    }
    val = mData[mPopIdx & (mSize - 1)];
    mData[mPopIdx & (mSize - 1)].~T(); // destroy old obj
    mPopIdx++;
    return true;
  }

private:
  size_t size() { return mPushIdx - mPopIdx; }
  bool empty() { return mPushIdx == mPopIdx; }

  T* mData{nullptr};
  size_t mSize{0u};
  size_t mPushIdx{0u};
  size_t mPopIdx{0u};
  std::mutex mLock; // mutex  because it is simple
};

int main() {
    SPSC<Ansh> q((1LL << 10)); // becuase of power of 2
    // assigned q as stack

    // Atomic because the main thread writes to stop while both
    // producer and consumer threads read it, avoiding a data race.
    std::atomic<bool> stop{false};



    // diff cache lines, no false sharing
    // alignas is crazy thing

    // changed int to long long because the no was huge really , not really
    alignas(64) ll pushed = 0;
    alignas(64) ll popped = 0;

    // Producer
    std::thread t1([&] {
        Ansh o{};

        while (!stop) {
            if (q.push(o)) {
                pushed++;
                o.id++;
            }
        }
    });

    // Consumer
    std::thread t2([&] {
        Ansh o{};
        ll idx = 0;

        while (!stop) {
            if (q.pop(o)) {
                if (o.id != idx) {

                    // can be any race condition just go on but it should not
                    // std::cout << "race  condition" << "\n"; // removed this because it taking a lot of time
                    // new thing learned
                }

                idx++;
                popped++;
            }
        }
    });

    // Runed benchmark for one second
    auto start = std::chrono::steady_clock::now();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    stop = true;

    t1.join();
    t2.join();

    auto end = std::chrono::steady_clock::now();

    double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << "Elapsed: " << seconds << " seconds\n";
    std::cout << "Pushed: " << pushed << ", Popped: " << popped << '\n';

    std::cout << "Push throughput: "<< pushed / seconds << " objects/sec\n";

    std::cout << "Pop throughput: "<< popped / seconds << " objects/sec\n";

    return 0;
}