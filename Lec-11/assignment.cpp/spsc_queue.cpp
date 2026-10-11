// Vansh Dobhal - 10099
// SPSC queue with std::mutex
// producer pushes 64 byte objects, consumer pops them, run for 1 second and count

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

using namespace std;

struct Obj {
    long long id;
    char pad[56];
};

const int SIZE = 1024;

class SPSC {
public:
    bool push(const Obj &val) {
        lock_guard<mutex> lg(mtx);
        if (count == SIZE)
            return false; // full
        data[pushIdx] = val;
        pushIdx = (pushIdx + 1) % SIZE;
        count++;
        return true;
    }

    bool pop(Obj &val) {
        lock_guard<mutex> lg(mtx);
        if (count == 0)
            return false; // empty
        val = data[popIdx];
        popIdx = (popIdx + 1) % SIZE;
        count--;
        return true;
    }

private:
    Obj data[SIZE]; // fixed buffer, no new/delete while running
    int pushIdx = 0;
    int popIdx = 0;
    int count = 0;
    mutex mtx;
};

SPSC q;
atomic<bool> stop(false);
long long pushed = 0;
long long popped = 0;
bool ok = true;

void producer() {
    Obj o;
    while (!stop) {
        o.id = pushed;
        if (q.push(o))
            pushed++;
    }
}

void consumer() {
    Obj o;
    while (!stop) {
        if (q.pop(o)) {
            if (o.id != popped)
                ok = false; // order got messed up
            popped++;
        }
    }
}

int main() {
    cout << "size of object = " << sizeof(Obj) << " bytes" << endl;

    thread t1(producer);
    thread t2(consumer);

    this_thread::sleep_for(chrono::seconds(1));
    stop = true;

    t1.join();
    t2.join();

    cout << "pushed in 1 sec = " << pushed << endl;
    cout << "popped in 1 sec = " << popped << endl;
    if (ok)
        cout << "order is correct" << endl;
    else
        cout << "order is wrong" << endl;
    return 0;
}
