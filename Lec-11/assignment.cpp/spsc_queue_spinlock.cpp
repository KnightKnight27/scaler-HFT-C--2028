// WRITE AN SPSC QUEUE
// SPINLOCK (WHILE LOOP) OR STD::MUTEX
// t1.join()  t2.join()
// producer consumer to push objects and pop objects
//
// you need to figure out a way that with locks how many
// 64 byte objects can u push and pop in 1 second
// raise a git PR for the same
// add readme for ur per second specs
// feel free to add worst code qaulity :)
//
//
// ^^ MEMORY POOL ^^

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

// Spinlock
// Uses a while loop to wait until the lock is available.
class SpinLock {
public:
    void lock() {
        while (mLock.test_and_set(std::memory_order_acquire)) {
            // Keep waiting until the lock becomes available.
        }
    }

    void unlock() {
        mLock.clear(std::memory_order_release);
    }

private:
    std::atomic_flag mLock = ATOMIC_FLAG_INIT;
};

// Memory Pool
// Allocates memory for multiple fixed-size blocks in one go.
class MemoryPool {
public:
    MemoryPool(std::size_t blockSize, std::size_t blockCount)
        : mBlockSize(blockSize), mBlockCount(blockCount) {

        if (blockSize == 0 || blockCount == 0) {
            throw std::invalid_argument("Invalid memory pool size");
        }

        // Allocate memory for all blocks at once.
        mMemory = ::operator new(mBlockSize * mBlockCount);
    }

    void* getBlock(std::size_t index) {
        if (index >= mBlockCount) {
            throw std::out_of_range("Invalid block index");
        }

        char* memory = static_cast<char*>(mMemory);
        return memory + index * mBlockSize;
    }

    ~MemoryPool() {
        ::operator delete(mMemory);
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

private:
    void* mMemory{nullptr};
    std::size_t mBlockSize{0};
    std::size_t mBlockCount{0};
};

// SPSC Queue
template <typename T>
class SPSC {
    public:
        // Constructor
        SPSC(std::size_t size)
            : mPool(sizeof(T), size), mSize(size) {
        }

        // Destructor
        ~SPSC() {
            // Destroying any objects still inside the queue
            for (std::size_t i = mPopIdx; i < mPushIdx; ++i) {
                T* item = static_cast<T*>(
                    mPool.getBlock(i % mSize)
                );

                item->~T();
            }
        }

        // No copying or moving
        SPSC(const SPSC<T>&) = delete;
        SPSC& operator=(const SPSC<T>&) = delete;
        SPSC(SPSC&&) = delete;
        SPSC& operator=(SPSC&&) = delete;

        // Push
        bool push(const T& val) {
            std::lock_guard<SpinLock> lock(mLock);

            // Queue full
            if (size() == mSize) {
                return false;
            }

            // Since we are using raw memory
            // We have to construct a T object with val at the specified memory address
            void* slot = mPool.getBlock(mPushIdx % mSize);
            new (slot) T(val);

            ++mPushIdx;
            return true;
        }

        // Pop
        bool pop(T& val) {
            std::lock_guard<SpinLock> lock(mLock);

            // Queue empty
            if (mPushIdx == mPopIdx) {
                return false;
            }

            T* item = static_cast<T*>(
                mPool.getBlock(mPopIdx % mSize)
            );

            val = *item;

            // Destroying object
            item->~T();
            ++mPopIdx;

            return true;
        }

    private:
        // Number of objects in the queue
        std::size_t size() const {
            return mPushIdx - mPopIdx;
        }

        MemoryPool mPool;  // Memory pool used to store queue objects

        std::size_t mSize{0};         // Queue Capacity
        std::size_t mPushIdx{0};      // Where the next object should be pushed
        std::size_t mPopIdx{0};       // Where the next object should be popped

        SpinLock mLock;
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