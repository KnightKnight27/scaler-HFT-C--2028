#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>


struct alignas(64) Object64 {
    char data[64];
};

template <typename T>
class SPSC {
private:
    size_t mSize;
    T* mData;

    size_t mPushIdx{0u};
    size_t mPopIdx{0u};
    
    // spinlock flag
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    SPSC(size_t size) : mSize(size) {
       
        mData = static_cast<T*>(::operator new(sizeof(T) * mSize));
    }

    ~SPSC() {
        ::operator delete(mData);
    }

    SPSC(const SPSC&) = delete;
    SPSC& operator=(const SPSC&) = delete;

    bool push(const T& val) {
        // lock acquire
        while (flag.test_and_set(std::memory_order_acquire)) {
           
        }
        
        bool succ = false;
        if (mPushIdx - mPopIdx != mSize) [[likely]] {
            // Power of 2 bitwise AND instead of Modulo
            mData[mPushIdx & (mSize - 1)] = val;
            mPushIdx++;
            succ = true;
        }

        // lock release
        flag.clear(std::memory_order_release);
        return succ;
    }

    bool pop(T& val) {
        // lock acquire
        while (flag.test_and_set(std::memory_order_acquire)) {
           
        }
        
        bool succ = false;
        if (mPushIdx != mPopIdx) [[likely]] {
            // bitwise & instead of %
            val = mData[mPopIdx & (mSize - 1)];
            mPopIdx++;
            succ = true;
        }

        // lock release
        flag.clear(std::memory_order_release);
        return succ;
    }
};


SPSC<Object64> q(1024);
std::atomic<bool> running{true};

// diff cache linw
alignas(64) long long push_cnt = 0;
alignas(64) long long pop_cnt = 0;

void producer() {
    Object64 obj;
    obj.data[0] = 'X'; 
    
    while (running.load(std::memory_order_relaxed)) {
        if (q.push(obj)) {
            push_cnt++;
        }
    }
}

void consumer() {
    Object64 obj;
    while (running.load(std::memory_order_relaxed) || pop_cnt < push_cnt) {
        if (q.pop(obj)) {
            pop_cnt++;
        }
    }
}

int main() {
    std::cout << "Starting Spinlock benchmark...\n";
    
    std::thread t1(producer);
    std::thread t2(consumer);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    running.store(false, std::memory_order_relaxed);

    t1.join();
    t2.join();

    std::cout << "Benchmark complete.\n";
  
    std::cout << "Objects Pushed : " << push_cnt << " ops/sec\n";
    std::cout << "Objects Popped : " << pop_cnt << " ops/sec\n";
    
    return 0;
}
