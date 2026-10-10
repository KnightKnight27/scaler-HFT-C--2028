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
#include <thread>
#include <atomic>
#include <chrono>


class SixtyFourBObject{
    char data[64]; 
};


class SpinLock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

class SPSCQueue{
    private:
        static constexpr int capacity = 2048; 
        alignas(64) int tail = 0;
        alignas(64) int head = 0;
        SixtyFourBObject queue[capacity];
        SpinLock lock;
    
    public:
        bool push(SixtyFourBObject newShi){
            lock.lock(); 
        
            if ((tail + 1) & (capacity-1) == head) {
                lock.unlock();
                return false; 
            }
        
            queue[tail & (capacity-1)] = newShi;
            tail = (tail + 1) & (capacity-1);
        
            lock.unlock(); 
            return true;
        }
        bool pop(SixtyFourBObject& oldShi){
            lock.lock(); 
        
            if (head == tail) {
                lock.unlock();
                return false; 
            }
        
            oldShi = queue[head & (capacity-1)];
            head = (head + 1) & (capacity-1);
        
            lock.unlock(); 
            return true;
        }
};

int main() {
    SPSCQueue q;
    std::atomic<bool> running{true};
    long long push_count = 0;
    long long pop_count = 0;


    std::thread t1([&]() {
        SixtyFourBObject obj;
        while (running.load()) {
            if (q.push(obj)) {
                push_count++;
            }
        }
    });

    std::thread t2([&]() {
        SixtyFourBObject obj;
        while (running.load()) {
            if (q.pop(obj)) {
                pop_count++;
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    running = false; 


    t1.join();
    t2.join();

    std::cout << "Pushes in 1 sec: " << push_count << "\n";
    std::cout << "Pops in 1 sec:   " << pop_count << "\n";

    return 0;
}
