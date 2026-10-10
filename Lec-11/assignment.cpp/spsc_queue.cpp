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

#include <iostream>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>

struct Obj {
    long long id;
    char pad[56];
};

struct Spin {
    std::atomic<bool> f{false};
    void lock() { while (f.exchange(true)) {} }
    void unlock() { f = false; }
};

const int N = 4096;

template <typename L>
struct Queue {
    std::vector<Obj> buf{N};
    long long head = 0, tail = 0;
    L lk;

    bool push(Obj& o) {
        std::lock_guard<L> g(lk);
        if (tail - head == N) return false;
        buf[tail % N] = o;
        tail++;
        return true;
    }

    bool pop(Obj& o) {
        std::lock_guard<L> g(lk);
        if (head == tail) return false;
        o = buf[head % N];
        head++;
        return true;
    }
};

template <typename L>
void run(const char* name) {
    Queue<L> q;
    std::atomic<bool> stop{false}, done{false};
    long long pushed = 0, popped = 0;

    std::thread t1([&] {
        Obj o{};
        while (!stop) {
            o.id = pushed;
            if (q.push(o)) pushed++;
        }
        done = true;
    });

    std::thread t2([&] {
        Obj o{};
        while (true) {
            bool d = done;
            if (q.pop(o)) popped++;
            else if (d) break;
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;
    t1.join();
    t2.join();

    std::cout << name << " pushed " << pushed << " popped " << popped << "\n";
}

int main() {
    std::cout << "size " << sizeof(Obj) << "\n";
    run<Spin>("spinlock");
    run<std::mutex>("mutex");
}
