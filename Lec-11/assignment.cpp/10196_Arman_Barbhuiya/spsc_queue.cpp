#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <vector>

// 1. The 64-Byte Object
struct Object64 {
    char payload[64];
};
static_assert(sizeof(Object64) == 64, "MUST BE EXACTLY 64 BYTES");

// 2. Memory Pool Node
struct Node {
    Object64 data;
    Node* next;
};

// 3. Memory Pool Implementation
class MemPool {
private:
    std::vector<Node> pool;
    Node* free_list;
    std::atomic_flag pool_lock = ATOMIC_FLAG_INIT;

public:
    MemPool(size_t count) {
        pool.resize(count);
        free_list = &pool[0];
        for (size_t i = 0; i < count - 1; ++i) {
            pool[i].next = &pool[i + 1];
        }
        pool[count - 1].next = nullptr;
    }

    Node* alloc() {
        while (pool_lock.test_and_set(std::memory_order_acquire)) {
            // spin
        }
        Node* node = free_list;
        if (node) {
            free_list = node->next;
        }
        pool_lock.clear(std::memory_order_release);
        return node;
    }

    void free_node(Node* node) {
        while (pool_lock.test_and_set(std::memory_order_acquire)) {
            // spin
        }
        node->next = free_list;
        free_list = node;
        pool_lock.clear(std::memory_order_release);
    }
};

// 4. SPSC Queue with Spinlock
class SPSCQueue {
private:
    Node* head;
    Node* tail;
    MemPool* pool;
    std::atomic_flag queue_lock = ATOMIC_FLAG_INIT;

public:
    SPSCQueue(MemPool* p) : pool(p) {
        head = pool->alloc();
        head->next = nullptr;
        tail = head;
    }

    void push(const Object64& obj) {
        Node* new_node = pool->alloc();
        if (!new_node) return; 
        
        new_node->data = obj;
        new_node->next = nullptr;

        while (queue_lock.test_and_set(std::memory_order_acquire)) {
            // spin in while loop
        }
        
        tail->next = new_node;
        tail = new_node;
        
        queue_lock.clear(std::memory_order_release);
    }

    bool pop(Object64& out_obj) {
        while (queue_lock.test_and_set(std::memory_order_acquire)) {
            // spin in while loop
        }
        
        Node* old_head = head;
        Node* next_node = old_head->next;

        if (!next_node) {
            queue_lock.clear(std::memory_order_release);
            return false;
        }

        out_obj = next_node->data;
        head = next_node;
        
        queue_lock.clear(std::memory_order_release);
        
        pool->free_node(old_head);
        return true;
    }
};

// Global state for benchmarking
SPSCQueue* g_queue;
MemPool* g_pool;
std::atomic<bool> g_start{false};
std::atomic<bool> g_done{false};
std::atomic<uint64_t> g_pushed{0};
std::atomic<uint64_t> g_popped{0};

void producer_thread_func() {
    while (!g_start.load(std::memory_order_acquire)) {}
    
    Object64 dummy_obj;
    std::memset(dummy_obj.payload, 'A', 64);
    
    while (!g_done.load(std::memory_order_acquire)) {
        g_queue->push(dummy_obj);
        g_pushed.fetch_add(1, std::memory_order_relaxed);
    }
}

void consumer_thread_func() {
    Object64 temp_obj;
    while (!g_start.load(std::memory_order_acquire)) {}
    
    while (true) {
        bool got_it = g_queue->pop(temp_obj);
        if (got_it) {
            g_popped.fetch_add(1, std::memory_order_relaxed);
        } else if (g_done.load(std::memory_order_acquire)) {
            break;
        }
    }
}

int main() {
    std::cout << "Initializing SPSC queue benchmark..." << std::endl;
    
    // Allocate Memory Pool once at startup
    g_pool = new MemPool(2000000);
    g_queue = new SPSCQueue(g_pool);
    
    std::thread t1(producer_thread_func);
    std::thread t2(consumer_thread_func);
    
    std::cout << "Starting benchmark for 1 second..." << std::endl;
    g_start.store(true, std::memory_order_release);
    
    // Run for exactly 1 second
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    g_done.store(true, std::memory_order_release);
    
    // Wait for threads to finish
    t1.join();
    t2.join();
    
    std::cout << "Benchmark finished." << std::endl;
    std::cout << "Total Pushed: " << g_pushed.load() << std::endl;
    std::cout << "Total Popped: " << g_popped.load() << std::endl;
    std::cout << "Object size: " << sizeof(Object64) << " bytes" << std::endl;
    
    delete g_queue;
    delete g_pool;
    return 0;
}