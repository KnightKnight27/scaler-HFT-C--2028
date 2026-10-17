#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <atomic>

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
            if(size() == capacity) {
                return false;
            }
            queue[pushIdx & (capacity-1)] = val;
            pushIdx++;
            return true;
        }

        bool pop(T& val) {
            if(empty()){
                return false;
            }
            val = queue[popIdx & (capacity-1)];
            popIdx++;
            return true;
        }
        

    private:
        size_t capacity = 0u;
        alignas(64) std::atomic<size_t> pushIdx;
        alignas(64) std::atomic<size_t> popIdx;
        size_t size() {
            return pushIdx - popIdx;
        }
        bool empty() {
            return pushIdx == popIdx;
        }
        std::vector<T> queue;
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