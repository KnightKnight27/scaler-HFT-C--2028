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
#include <array>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

struct Object64
{
    alignas(64) std::int64_t num;
};

template <std::size_t Capacity>
class SPSCQueue
{
private:
    std::array<Object64, Capacity> buffer;
    std::size_t head = 0;
    std::size_t tail = 0;
    std::size_t count = 0;

    std::mutex mtx;

public:
    bool push(const Object64 &obj)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (count == Capacity)
        {
            return false;
        }
        buffer[tail] = obj;
        tail = (tail + 1) % Capacity;
        count++;

        return true;
    }
    bool pop(Object64 &outObj)
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (count == 0)
        {
            return false;
        }
        outObj = buffer[head];
        head = (head + 1) % Capacity;
        count--;

        return true;
    }
};

void Producer(SPSCQueue<1024> &queue, std::atomic<bool> &running, std::uint64_t &pushCount)
{
    while (running)
    {
        Object64 obj{1};
        if (queue.push(obj))
        {
            pushCount++;
        }
    }
}

void Consumer(SPSCQueue<1024> &queue, std::atomic<bool> &running, std::uint64_t &popCount)
{
    while (running)
    {
        Object64 outObj{};
        if (queue.pop(outObj))
        {
            popCount++;
        }
    }
}

int main()
{
    SPSCQueue<1024> queue;
    std::atomic<bool> running{true};
    std::uint64_t pushCount = 0;
    std::uint64_t popCount = 0;
    std::thread t1(Producer, std::ref(queue), std::ref(running), std::ref(pushCount));
    std::thread t2(Consumer, std::ref(queue), std::ref(running), std::ref(popCount));

    std::this_thread::sleep_for(std::chrono::seconds(1));

    running = false;

    t1.join();
    t2.join();

    std::cout << "Successful pushes:                      " << pushCount << '\n';
    std::cout << "Successful pops:                        " << popCount << '\n';
    std::cout << "Total push and pops in 1 second:        " << (pushCount + popCount) << '\n';
}
