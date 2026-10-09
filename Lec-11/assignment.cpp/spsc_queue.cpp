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

using namespace std;

// the object we push, exactly 64 bytes
struct Obj {
    long long id;
    char data[56];
};

// simple spinlock using atomic_flag, just keeps looping till it gets the lock
struct SpinLock {
    atomic_flag flag = ATOMIC_FLAG_INIT;

    void lock() {
        while (flag.test_and_set(memory_order_acquire)) {
            // spin
        }
    }
    void unlock() {
        flag.clear(memory_order_release);
    }
};

// ring buffer queue. the array is allocated once at the start
// so we never call new while pushing/popping (this is the memory pool part)
template <typename LockType>
class SPSCQueue {
    Obj* buf;
    int cap;
    int head; // consumer reads from here
    int tail; // producer writes here
    int count;
    LockType lk;

public:
    SPSCQueue(int size) {
        cap = size;
        buf = new Obj[cap];
        head = 0;
        tail = 0;
        count = 0;
    }

    ~SPSCQueue() {
        delete[] buf;
    }

    bool push(const Obj& o) {
        lk.lock();
        if (count == cap) {
            lk.unlock();
            return false; // full
        }
        buf[tail] = o;
        tail++;
        if (tail == cap) tail = 0;
        count++;
        lk.unlock();
        return true;
    }

    bool pop(Obj& o) {
        lk.lock();
        if (count == 0) {
            lk.unlock();
            return false; // empty
        }
        o = buf[head];
        head++;
        if (head == cap) head = 0;
        count--;
        lk.unlock();
        return true;
    }
};

template <typename LockType>
void runTest(string name) {
    SPSCQueue<LockType> q(1024);
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
