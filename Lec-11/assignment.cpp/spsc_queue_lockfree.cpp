#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <atomic>
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
            // Getting the current push index
            std::size_t pushIdx = mPushIdx.load(
                std::memory_order_relaxed
            );

            // Getting the current pop index
            std::size_t popIdx = mPopIdx.load(
                std::memory_order_acquire
            );

            // Queue full
            if (pushIdx - popIdx == mSize) [[unlikely]] {
                return false;
            }

            mBuffer[pushIdx & (mSize - 1)] = val;

            mPushIdx.store(
                pushIdx + 1,
                std::memory_order_release
            );

            return true;
        }

        // Pop
        bool pop(const T*& val) {
            // Checking whether the previous object is still being used
            if (mItemInUse) [[unlikely]] {
                return false;
            }

            // Getting the current pop index
            std::size_t popIdx = mPopIdx.load(
                std::memory_order_relaxed
            );

            // Getting the current push index
            std::size_t pushIdx = mPushIdx.load(
                std::memory_order_acquire
            );

            // Queue empty
            if (pushIdx == popIdx) [[unlikely]] {
                return false;
            }

            val = &mBuffer[popIdx & (mSize - 1)];

            // The slot cannot be reused until release() is called
            mItemInUse = true;

            return true;
        }

        // Release
        void release() {
            if (mItemInUse) {
                std::size_t popIdx = mPopIdx.load(
                    std::memory_order_relaxed
                );

                mPopIdx.store(
                    popIdx + 1,
                    std::memory_order_release
                );

                mItemInUse = false;
            }
        }

        bool isLockFree() const {
            return mPushIdx.is_lock_free() && mPopIdx.is_lock_free();
        }

    private:
        std::vector<T> mBuffer;  // Queue buffer

        std::size_t mSize{0};         // Queue Capacity

        alignas(64) std::atomic<std::size_t> mPushIdx{0};
        alignas(64) std::atomic<std::size_t> mPopIdx{0};

        // Used by the consumer thread
        bool mItemInUse{false};
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
        const Message* msg = nullptr;

        while (std::chrono::steady_clock::now() < end) {
            if (queue.pop(msg)) {
                // Use msg here before releasing the slot
                // The pointer must not be used after release()
                queue.release();

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
    std::cout << "Atomic indices are lock-free: "
          << std::boolalpha
          << queue.isLockFree() << '\n';
    std::cout << "Successful pushes: " << pushCount << '\n';
    std::cout << "Successful pops: " << popCount << '\n';

    std::cout << "Push throughput: " << pushCount / elapsed
              << " objects/second\n";

    std::cout << "Pop throughput: " << popCount / elapsed
              << " objects/second\n";

    return 0;
}
