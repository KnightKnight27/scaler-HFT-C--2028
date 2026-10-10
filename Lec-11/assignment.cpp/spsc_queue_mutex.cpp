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

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <vector>

// SPSC Queue
template <typename T>
class SPSC {
    public:
        // Constructor
        SPSC(std::size_t size)
            : mBuffer(size), mSize(size) {

            if (size == 0 || (size & (size - 1)) != 0) {
                throw std::invalid_argument(
                    "Queue size must be a power of two"
                );
            }
        }

        // Destructor
        ~SPSC() = default;

        // No copying or moving
        SPSC(const SPSC<T>&) = delete;
        SPSC& operator=(const SPSC<T>&) = delete;
        SPSC(SPSC&&) = delete;
        SPSC& operator=(SPSC&&) = delete;

        // Push
        bool push(const T& val) {
            std::lock_guard<std::mutex> lock(mMutex);

            // Queue full
            if (size() == mSize) {
                return false;
            }

            mBuffer[mPushIdx & (mSize - 1)] = val;

            ++mPushIdx;
            return true;
        }

        // Pop
        bool pop(T& val) {
            std::lock_guard<std::mutex> lock(mMutex);

            // Queue empty
            if (mPushIdx == mPopIdx) {
                return false;
            }

            val = mBuffer[mPopIdx & (mSize - 1)];

            ++mPopIdx;

            return true;
        }

    private:
        // Number of objects in the queue
        std::size_t size() const {
            return mPushIdx - mPopIdx;
        }

        std::vector<T> mBuffer;  // Queue buffer

        std::size_t mSize{0};         // Queue Capacity
        std::size_t mPushIdx{0};      // Where the next object should be pushed
        std::size_t mPopIdx{0};       // Where the next object should be popped

        std::mutex mMutex;
};

struct Message {
    char data[64];
};

static_assert(sizeof(Message) == 64, "Message exactly of 64 bytes");


int main() {
    SPSC<Message> queue(1024);

    std::uint64_t pushCount = 0;
    std::uint64_t popCount = 0;

    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::seconds(1);

    std::thread t1([&]() {
        Message msg{};

        while (std::chrono::steady_clock::now() < end) {
            if (queue.push(msg)) {
                ++pushCount;
            }
        }
    });

    std::thread t2([&]() {
        Message msg{};

        while (std::chrono::steady_clock::now() < end) {
            if (queue.pop(msg)) {
                ++popCount;
            }
        }
    });

    t1.join();
    t2.join();

    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start
    ).count();

    std::cout << "Object size: " << sizeof(Message) << " bytes\n";
    std::cout << "Queue capacity: 1024 objects\n";
    std::cout << "Successful pushes: " << pushCount << '\n';
    std::cout << "Successful pops: " << popCount << '\n';

    std::cout << "Push throughput: " << pushCount / elapsed
              << " objects/second\n";

    std::cout << "Pop throughput: " << popCount / elapsed
              << " objects/second\n";

    return 0;
}
