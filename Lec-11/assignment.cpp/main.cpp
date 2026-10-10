#include "spsc_queue.hpp"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t queue_capacity = 1024;
constexpr std::uint64_t salt = 0x9e3779b97f4a7c15ULL;

hft::Packet64 make_packet(std::uint64_t sequence) {
    hft::Packet64 packet;
    for (std::size_t word = 0; word < packet.words.size(); ++word) {
        packet.words[word] = sequence ^ (salt * (word + 1));
    }
    return packet;
}

struct StartGate {
    std::mutex mutex;
    std::condition_variable changed;
    unsigned ready = 0;
    bool open = false;
    bool cancelled = false;
    Clock::time_point deadline;

    bool wait() {
        std::unique_lock<std::mutex> lock(mutex);
        ++ready;
        changed.notify_all();
        changed.wait(lock, [&] { return open; });
        return !cancelled;
    }
};

struct Sample {
    std::uint64_t pushed_in_window = 0;
    std::uint64_t popped_in_window = 0;
    std::uint64_t pushed_total = 0;
    std::uint64_t popped_total = 0;
    std::uint64_t full_retries = 0;
    std::uint64_t empty_retries = 0;
    bool valid = true;
};

template <typename Mutex>
Sample measure() {
    hft::BoundedSpscQueue<queue_capacity, Mutex> queue;
    StartGate gate;
    std::atomic<bool> producer_done{false};
    Sample sample;
    std::thread producer;
    std::thread consumer;

    try {
        producer = std::thread([&] {
            if (!gate.wait()) return;
            auto packet = make_packet(0);
            while (Clock::now() < gate.deadline) {
                if (queue.try_push(packet)) {
                    if (Clock::now() < gate.deadline) ++sample.pushed_in_window;
                    ++sample.pushed_total;
                    packet = make_packet(sample.pushed_total);
                } else {
                    ++sample.full_retries;
                    std::this_thread::yield();
                }
            }
            producer_done.store(true, std::memory_order_release);
        });

        consumer = std::thread([&] {
            if (!gate.wait()) return;
            hft::Packet64 packet;
            const auto validate = [&] {
                if (packet.words != make_packet(sample.popped_total).words) {
                    sample.valid = false;
                }
                ++sample.popped_total;
            };
            while (Clock::now() < gate.deadline) {
                if (queue.try_pop(packet)) {
                    if (Clock::now() < gate.deadline) ++sample.popped_in_window;
                    validate();
                } else {
                    ++sample.empty_retries;
                    std::this_thread::yield();
                }
            }
            // Drain separately: these packets are excluded from timed throughput.
            while (!producer_done.load(std::memory_order_acquire)) {
                if (queue.try_pop(packet)) validate();
                else std::this_thread::yield();
            }
            // Recheck after done: the last push can race an earlier empty pop.
            while (queue.try_pop(packet)) validate();
        });
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(gate.mutex);
            gate.cancelled = true;
            gate.open = true;
        }
        gate.changed.notify_all();
        if (producer.joinable()) producer.join();
        if (consumer.joinable()) consumer.join();
        throw;
    }

    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        gate.changed.wait(lock, [&] { return gate.ready == 2; });
        gate.deadline = Clock::now() + std::chrono::seconds(1);
        gate.open = true;
    }
    gate.changed.notify_all();
    producer.join();
    consumer.join();
    sample.valid = sample.valid && sample.pushed_total == sample.popped_total;
    return sample;
}

void print_sample(unsigned run, const char* name, const Sample& sample) {
    std::cout << run << ',' << name << ',' << sample.pushed_in_window << ','
              << sample.popped_in_window << ',' << sample.pushed_total << ','
              << sample.popped_total << ','
              << sample.popped_total - sample.popped_in_window << ','
              << sample.full_retries << ',' << sample.empty_retries << ','
              << (sample.valid ? "PASS" : "FAIL") << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::invalid_argument("too many arguments");
        unsigned runs = 5;
        if (argc == 2) {
            const std::string text(argv[1]);
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
                throw std::invalid_argument("invalid run count");
            }
            const auto count = std::stoul(text);
            if (count == 0 || count > 100) throw std::invalid_argument("run count out of range");
            runs = static_cast<unsigned>(count);
        }
        std::cout << "# payload_bytes=" << sizeof(hft::Packet64)
                  << ",capacity=" << queue_capacity << ",window_seconds=1"
                  << ",hardware_threads=" << std::thread::hardware_concurrency() << '\n';
        std::cout << "run,lock,pushes_per_second,pops_per_second,total_pushed,total_popped,"
                     "post_window_pops,full_retries,empty_retries,validation\n";
        for (unsigned run = 0; run < runs; ++run) {
            Sample mutex_sample, spin_sample;
            // Alternate order to avoid always measuring one lock second.
            if (run % 2 == 0) {
                mutex_sample = measure<std::mutex>();
                spin_sample = measure<hft::SpinMutex>();
            } else {
                spin_sample = measure<hft::SpinMutex>();
                mutex_sample = measure<std::mutex>();
            }
            print_sample(run + 1, "std::mutex", mutex_sample);
            print_sample(run + 1, "SpinMutex", spin_sample);
            if (!mutex_sample.valid || !spin_sample.valid) return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\nUsage: spsc_bench [runs: 1..100]\n";
        return 1;
    }
}
