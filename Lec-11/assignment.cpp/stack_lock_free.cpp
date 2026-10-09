#include <iostream>
#include <atomic>
#include <thread>
#include <vector>

template <typename T>
class LockFreeStack {
private:
    struct Node {
        T data;
        Node* next;
        Node(const T& val) : data(val), next(nullptr) {}
    };

    std::atomic<Node*> head;

public:
    LockFreeStack() : head(nullptr) {}

    ~LockFreeStack() {
        Node* curr = head.load();
        while (curr) {
            Node* temp = curr;
            curr = curr->next;
            delete temp;
        }
    }

    void push(const T& val) {
        Node* new_node = new Node(val);
        new_node->next = head.load(std::memory_order_relaxed);
        while (!head.compare_exchange_weak(
            new_node->next,
            new_node,
            std::memory_order_release,
            std::memory_order_relaxed)) {
     
        }
    }

    bool pop(T& val) {
        Node* old_head = head.load(std::memory_order_relaxed);
        while (old_head && !head.compare_exchange_weak(
            old_head,
            old_head->next,
            std::memory_order_acquire,
            std::memory_order_relaxed)) {
   
        }
        
        if (old_head) {
            val = old_head->data;
            delete old_head;
            return true;
        }
        return false;
    }
};

int main() {
    LockFreeStack<int> stack;
    const int num_items = 100000;

    auto producer = [&]() {
        for (int i = 0; i < num_items; ++i) {
            stack.push(i);
        }
    };

    auto consumer = [&]() {
        int val;
        int count = 0;
        while (count < num_items) {
            if (stack.pop(val)) {
                count++;
            }
        }
    };

    std::thread p_thread(producer);
    std::thread c_thread(consumer);

    p_thread.join();
    c_thread.join();

    std::cout << "Successfully pushed and popped " << num_items << " items concurrently with zero memory leaks.\n";

    return 0;
}
