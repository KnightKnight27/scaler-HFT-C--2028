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

// ===========================================================

#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>

template <typename T>

class SPSCQueue{
    public:
        SPSCQueue(){};
        SPSCQueue(size_t size){
            capacity = size;
            queue.resize(capacity);
            pushIdx = 0;
            popIdx = 0;
        }
        
        bool push(const T& val) {
            mtx.lock();
            if(size() == capacity) {
                mtx.unlock();
                return false;
            }
            queue[pushIdx & (capacity-1)] = val;
            pushIdx++;
            mtx.unlock();
            return true;
        }

        bool pop(T& val) {
            mtx.lock();
            if(empty()){
                mtx.unlock();
                return false;
            }
            val = queue[popIdx & (capacity-1)];
            popIdx++;
            mtx.unlock();
            return true;
        }
        

    private:
        size_t capacity = 0u;
        size_t pushIdx;
        size_t popIdx;
        size_t size() {
            return pushIdx - popIdx;
        }
        bool empty() {
            return pushIdx == popIdx;
        }
        std::vector<T> queue;
        std::mutex mtx;
};

struct alignas(64) data{
    char arr[64];
};

void producer(SPSCQueue<data>& q) {
    for(int i=0;i<10000000;i++){
        while(!q.push(data())){
            std::this_thread::yield();
        }
    }
}

void consumer(SPSCQueue<data>& q) {
    data val;
    for(int i=0;i<10000000;i++){
        while(!q.pop(val)){
            std::this_thread::yield();
        }
    }
}
 
int main() {
    SPSCQueue<data> q(1024);

    auto start = std::chrono::high_resolution_clock::now();
    std::thread producerThread(producer, std::ref(q));
    std::thread consumerThread(consumer, std::ref(q));
    producerThread.join();
    consumerThread.join();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    std::cout << "Time taken: " << duration.count() << " seconds" << std::endl;
    std::cout << "Throughput: " << 10000000 / duration.count() << " objects per second" << std::endl;
    return 0;
}