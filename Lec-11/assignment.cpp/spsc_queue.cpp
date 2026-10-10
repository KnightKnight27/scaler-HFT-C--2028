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
#include "spsc_queue_mutex.cpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

// one message = 64 bytes
struct Msg64 {
    char data[64];
};
static_assert(sizeof(Msg64) == 64, "Msg64 must be 64 bytes");

// the queue is created once (memory pool), so no new/delete while running
MutexSPSCQueue<Msg64> q(65536);

std::atomic<bool> producerDone(false); // producer tells consumer "i'm finished"
long pushed = 0;                       // only the producer changes this
long popped = 0;                       // only the consumer changes this

// t1: push messages for 1 second
void producer() {
    Msg64 msg{};
    msg.data[0] = 42;

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
        if (q.push(msg)) {
            pushed++;
        }
    }
    producerDone = true;
}

// t2: pop messages until the producer is done
void consumer() {
    while (!producerDone) {
        if (q.pop([](Msg64 &) {})) {
            popped++;
        }
    }
    // pop whatever is still left in the queue
    while (q.pop([](Msg64 &) {})) {
        popped++;
    }
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);

    t1.join();
    t2.join();

    std::cout << "objects pushed in 1 second: " << pushed << "\n";
    std::cout << "objects popped in 1 second: " << popped << "\n";
    return 0;
}
