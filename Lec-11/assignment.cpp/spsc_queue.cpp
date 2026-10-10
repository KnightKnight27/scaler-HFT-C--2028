// lec-11 assignment - SPSC queue
// Name: Tanishka Mangure - Roll: 24bcs10264
// spsc means single producer single consumer
#include <bits/stdc++.h>
using namespace std;

// 64 byte object
struct Data {
    char x[64];
};

// memory pool, made big array only once so no new malloc again and again
#define MAX 8192
Data pool[MAX];
int head = 0, tail = 0, cnt = 0;

mutex m; // using mutex, spinlock also tried below
atomic_flag mylock = ATOMIC_FLAG_INIT;

int use_spin = 0; // 0 = mutex, 1 = spinlock

void mylock_on() {
    if (use_spin == 0) m.lock();
    else { while (mylock.test_and_set()) {} } // spinlock while loop
}
void mylock_off() {
    if (use_spin == 0) m.unlock();
    else mylock.clear();
}

int myPush(Data d) {
    mylock_on();
    if (cnt == MAX) { mylock_off(); return 0; }
    pool[tail] = d;
    tail++;
    if (tail == MAX) tail = 0;
    cnt++;
    mylock_off();
    return 1;
}

int myPop(Data &d) {
    mylock_on();
    if (cnt == 0) { mylock_off(); return 0; }
    d = pool[head];
    head++;
    if (head == MAX) head = 0;
    cnt--;
    mylock_off();
    return 1;
}

long long pushed = 0, popped = 0;
int stop = 0;

void producer() {
    Data d;
    for (int i = 0; i < 64; i++) d.x[i] = 'A' + (i % 26);
    long long c = 0;
    while (stop == 0) {
        if (myPush(d) == 1) c++;
    }
    pushed = c;
}

void consumer() {
    Data d;
    long long c = 0;
    while (stop == 0) {
        if (myPop(d) == 1) {
            c++;
            // just to use data so compiler dont remove it
            if (d.x[0] == 'Z') cout << "";
        }
    }
    popped = c;
}

long long run_test(string name) {
    head = 0; tail = 0; cnt = 0;
    pushed = 0; popped = 0;
    stop = 0;

    thread t1(producer); // producer
    thread t2(consumer); // consumer

    this_thread::sleep_for(chrono::seconds(1)); // run for 1 sec
    stop = 1;

    t1.join();
    t2.join();

    cout << name << " pushed=" << pushed << " popped=" << popped << " (64B objs, 1 sec)" << endl;
    return popped;
}

int main() {
    cout << "SPSC 64B throughput (1-sec window, queue 8192, memory pool array)" << endl;

    use_spin = 0;
    long long a = run_test("mutex:     ");

    // reset spinlock flag just in case
    mylock.clear();
    use_spin = 1;
    long long b = run_test("spinlock:  ");

    if (b > a) cout << "winner: spinlock (mutex=" << a << "/s spin=" << b << "/s)" << endl;
    else cout << "winner: mutex (mutex=" << a << "/s spin=" << b << "/s)" << endl;

    return 0;
}
