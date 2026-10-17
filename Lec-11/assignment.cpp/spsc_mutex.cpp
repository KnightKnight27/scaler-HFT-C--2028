#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

using namespace std;

struct Object64 {
    char data[64];
};

#define QUEUE_SIZE 1024

Object64 arr[QUEUE_SIZE];
int head = 0;
int tail = 0;
int count_val = 0;
mutex mtx;

bool push_item(Object64 val) {
    mtx.lock();
    if (count_val >= QUEUE_SIZE) {
        mtx.unlock();
        return false;
    }
    arr[tail] = val;
    tail = (tail + 1) % QUEUE_SIZE;
    count_val = count_val + 1;
    mtx.unlock();
    return true;
}

bool pop_item(Object64 &val) {
    mtx.lock();
    if (count_val <= 0) {
        mtx.unlock();
        return false;
    }
    val = arr[head];
    head = (head + 1) % QUEUE_SIZE;
    count_val = count_val - 1;
    mtx.unlock();
    return true;
}

atomic<bool> running{true};
long long push_count = 0;
long long pop_count = 0;

void producer() {
    Object64 obj;
    for (int i = 0; i < 64; i++) {
        obj.data[i] = (char)(i % 128);
    }

    while (running.load()) {
        if (push_item(obj)) {
            push_count++;
        }
    }
}

void consumer() {
    Object64 obj;
    while (running.load()) {
        if (pop_item(obj)) {
            pop_count++;
        }
    }
}

int main() {
    cout << "sizeof Object64: " << sizeof(Object64) << " bytes" << endl;
    cout << "running mutex benchmark for 1 sec..." << endl;

    thread t1(producer);
    thread t2(consumer);

    this_thread::sleep_for(chrono::seconds(1));
    running.store(false);

    t1.join();
    t2.join();

    cout << "RESULT" << endl;
    cout << "pushed objects in 1s: " << push_count << endl;
    cout << "popped objects in 1s: " << pop_count << endl;

    return 0;
}
