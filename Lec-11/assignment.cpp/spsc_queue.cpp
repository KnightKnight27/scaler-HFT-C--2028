#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

struct Object {
    std::array<std::uint64_t, 8> words{};
};
static_assert(sizeof(Object) == 64, "Each queued object must be 64 bytes");

Object make_object(std::uint64_t sequence) {
    Object object;
    for (std::size_t i = 0; i < object.words.size(); ++i) {
        object.words[i] = sequence + i;
    }
    return object;
}

bool matches(const Object& object, std::uint64_t sequence) {
    for (std::size_t i = 0; i < object.words.size(); ++i) {
        if (object.words[i] != sequence + i) return false;
    }
    return true;
}

// The preallocated ring is the object pool: popped slots are reused on wraparound.
// One producer and one consumer use this queue; all shared state uses one mutex.
class SPSCQueue {
public:
    enum class PopResult { value, empty, closed };

    explicit SPSCQueue(std::size_t capacity) : pool_(capacity) {
        if (capacity == 0) throw std::invalid_argument("capacity must be positive");
    }

    bool try_push(const Object& object) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || size_ == pool_.size()) return false;
        pool_[tail_] = object;
        tail_ = (tail_ + 1) % pool_.size();
        ++size_;
        return true;
    }

    PopResult try_pop(Object& object) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == 0) return closed_ ? PopResult::closed : PopResult::empty;
        object = pool_[head_];
        head_ = (head_ + 1) % pool_.size();
        --size_;
        return PopResult::value;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }

private:
    std::vector<Object> pool_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
    bool closed_ = false;
    std::mutex mutex_;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_queue() {
    using Result = SPSCQueue::PopResult;
    bool rejected_zero = false;
    try { SPSCQueue invalid(0); }
    catch (const std::invalid_argument&) { rejected_zero = true; }
    require(rejected_zero, "zero capacity was accepted");

    SPSCQueue queue(3);
    Object object;
    require(queue.try_pop(object) == Result::empty, "new queue is not empty");
    for (std::uint64_t i = 0; i < 3; ++i) {
        require(queue.try_push(make_object(i)), "push failed before full");
    }
    require(!queue.try_push(make_object(99)), "full queue accepted an object");
    require(queue.try_pop(object) == Result::value && matches(object, 0), "FIFO failed");
    require(queue.try_push(make_object(3)), "wraparound push failed");
    queue.close();
    require(!queue.try_push(make_object(4)), "closed queue accepted an object");
    for (std::uint64_t i = 1; i <= 3; ++i) {
        require(queue.try_pop(object) == Result::value && matches(object, i),
                "closed queue did not drain in FIFO order");
    }
    require(queue.try_pop(object) == Result::closed, "drained queue is not closed");

    // Tiny capacities exercise full/empty transitions and repeated wraparound.
    for (std::size_t capacity : {1U, 3U, 4096U}) {
        SPSCQueue concurrent(capacity);
        constexpr std::uint64_t count = 100000;
        std::uint64_t consumed = 0;
        bool valid = true;
        std::thread producer([&] {
            for (std::uint64_t i = 0; i < count; ++i) {
                const Object next = make_object(i);
                while (!concurrent.try_push(next)) std::this_thread::yield();
            }
            concurrent.close();
        });
        std::thread consumer([&] {
            Object next;
            for (;;) {
                const auto result = concurrent.try_pop(next);
                if (result == Result::closed) break;
                if (result == Result::empty) {
                    std::this_thread::yield();
                    continue;
                }
                valid = matches(next, consumed) && valid;
                ++consumed;
            }
        });
        producer.join();
        consumer.join();
        require(valid && consumed == count, "concurrent FIFO/payload validation failed");
    }
    std::cout << "Tests passed: empty, full, wraparound, close/drain, zero capacity, "
                 "and 100000 transfers each at capacities 1, 3, 4096.\n";
}

struct BenchmarkResult {
    std::uint64_t pushed_in_window = 0;
    std::uint64_t popped_in_window = 0;
    std::uint64_t total_pushed = 0;
    std::uint64_t total_popped = 0;
    double elapsed_with_drain = 0;
    bool valid = true;
};

BenchmarkResult benchmark(double seconds) {
    using Clock = std::chrono::steady_clock;
    SPSCQueue queue(4096);
    BenchmarkResult result;
    std::mutex start_mutex;
    std::condition_variable start_cv;
    unsigned ready = 0;
    bool started = false;
    Clock::time_point start, deadline;

    auto wait_for_start = [&] {
        std::unique_lock<std::mutex> lock(start_mutex);
        ++ready;
        start_cv.notify_all();
        start_cv.wait(lock, [&] { return started; });
    };

    std::thread producer([&] {
        wait_for_start();
        Object next = make_object(0);
        while (Clock::now() < deadline) {
            if (queue.try_push(next)) {
                if (Clock::now() < deadline) ++result.pushed_in_window;
                ++result.total_pushed;
                next = make_object(result.total_pushed);
            } else {
                std::this_thread::yield();
            }
        }
        queue.close();
    });

    Clock::time_point finished;
    std::thread consumer([&] {
        wait_for_start();
        Object next;
        for (;;) {
            const auto status = queue.try_pop(next);
            if (status == SPSCQueue::PopResult::closed) break;
            if (status == SPSCQueue::PopResult::empty) {
                std::this_thread::yield();
                continue;
            }
            // Count only pops that complete inside the timed window.
            if (Clock::now() < deadline) ++result.popped_in_window;
            result.valid = matches(next, result.total_popped) && result.valid;
            ++result.total_popped;
        }
        finished = Clock::now();
    });

    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_cv.wait(lock, [&] { return ready == 2; });
        start = Clock::now();
        deadline = start + std::chrono::duration_cast<Clock::duration>(
                               std::chrono::duration<double>(seconds));
        started = true;
    }
    start_cv.notify_all();
    producer.join();
    consumer.join();
    result.elapsed_with_drain = std::chrono::duration<double>(finished - start).count();
    require(result.valid && result.total_pushed == result.total_popped,
            "benchmark lost, reordered, or corrupted an object");
    return result;
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--test") {
            test_queue();
            return 0;
        }
        if (argc != 1) {
            std::cerr << "Usage: " << argv[0] << " [--test]\n";
            return 1;
        }
        benchmark(0.2); // Untimed setup and a short warmup before reported runs.
        std::cout << "std::mutex SPSC queue; object=64 bytes; capacity=4096; window=1 second\n"
                  << "run,pushes_in_1s,pops_in_1s,drained_after_1s,total_transferred,"
                     "elapsed_with_drain_s,payload_MiB_per_s\n";
        for (unsigned run = 1; run <= 3; ++run) {
            const auto result = benchmark(1.0);
            const double mib = static_cast<double>(result.popped_in_window) * 64 / (1024 * 1024);
            std::cout << run << ',' << result.pushed_in_window << ',' << result.popped_in_window
                      << ',' << result.total_popped - result.popped_in_window
                      << ',' << result.total_popped << ',' << std::fixed << std::setprecision(6)
                      << result.elapsed_with_drain << ',' << std::setprecision(2) << mib << '\n';
        }
        std::cout << "Validation passed: every pushed object was popped in FIFO order, "
                     "with all 64 bytes checked.\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
