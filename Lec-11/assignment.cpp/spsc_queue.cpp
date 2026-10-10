#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

template <typename T>
class SPSCQueue
{
public:
    SPSCQueue(std::size_t cap = 2048)
        : capacity(cap), buffer(cap)
    {
    }

    bool push(const T &val)
    {
        std::lock_guard<std::mutex> lock(mutex_lock);
        if (current_size == capacity)
        {
            return false;
        }
        buffer[push_idx] = val;
        push_idx = (push_idx + 1) % capacity;
        current_size++;

        return true;
    }

    bool pop(T &val_tobe_out)
    {
        std::lock_guard<std::mutex> lock(mutex_lock);

        if (current_size == 0)
        {
            return false;
        }
        val_tobe_out = buffer[pop_idx];
        pop_idx = (pop_idx + 1) % capacity;
        current_size--;

        return true;
    }

private:
    std::size_t capacity;
    std::vector<T> buffer;
    std::size_t pop_idx = 0;
    std::size_t push_idx = 0;
    std::size_t current_size = 0;

    std::mutex mutex_lock;
};

template <typename T>
void Producer(SPSCQueue<T> &queue, std::atomic<bool> &running, std::uint64_t &pushCount){
    while (running){
        T obj{1};
        if (queue.push(obj)){
            pushCount++;
        }}
}

template <typename T>
void Consumer(SPSCQueue<T> &queue, std::atomic<bool> &running, std::uint64_t &popCount){
    while (running){
        T outObj{};
        if (queue.pop(outObj)){
            popCount++;
        }}
}

int main()
{
    SPSCQueue<std::int64_t> queue(2048);
    std::atomic<bool> running{true};
    std::uint64_t pushCount = 0;
    std::uint64_t popCount = 0;
    std::thread t1(Producer<std::int64_t>, std::ref(queue), std::ref(running), std::ref(pushCount));
    std::thread t2(Consumer<std::int64_t>, std::ref(queue), std::ref(running), std::ref(popCount));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    running = false;
    t1.join();
    t2.join();
    std::cout << "total pushes done :  " << pushCount << '\n';
    std::cout << "total pops done :  " << popCount << '\n';
    std::cout << "total pushes and pops done in 1 second: " << (pushCount + popCount) << '\n';
}
