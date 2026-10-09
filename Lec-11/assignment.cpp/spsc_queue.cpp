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

// compile: g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
// run:     ./spsc_queue

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>

#include "spsc_queue.hpp"

using namespace std;

// the object we push, exactly 64 bytes
struct Obj {
    long long id;
    char data[56];
};

static_assert(sizeof(Obj) == 64, "Obj should be 64 bytes");

template <typename LockType>
void runTest(string name) {
    SPSCQueue<Obj, LockType> q(1024);
    atomic<bool> stop(false);
    long long pushed = 0;
    long long popped = 0;
    bool wrongOrder = false;

    thread t1([&]() {
        Obj o;
        memset(o.data, 'a', sizeof(o.data));
        long long i = 0;
        while (!stop) {
            o.id = i;
            if (q.push(o)) {
                i++;
            }
        }
        pushed = i;
    });

    thread t2([&]() {
        Obj o;
        long long expect = 0;
        while (!stop) {
            if (q.pop(o)) {
                if (o.id != expect) wrongOrder = true;
                expect++;
            }
        }
        popped = expect;
    });

    this_thread::sleep_for(chrono::seconds(1));
    stop = true;

    t1.join();
    t2.join();

    cout << name << endl;
    cout << "  pushed in 1 sec : " << pushed << endl;
    cout << "  popped in 1 sec : " << popped << endl;
    cout << "  MB/s popped     : " << (popped * 64) / (1024 * 1024) << endl;
    if (wrongOrder) cout << "  ERROR: order was wrong!!" << endl;
    else cout << "  order ok" << endl;
}

int main() {
    cout << "sizeof(Obj) = " << sizeof(Obj) << " bytes" << endl << endl;

    runTest<mutex>("std::mutex");
    cout << endl;
    runTest<SpinLock>("spinlock");

    return 0;
}
