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
#include <mutex>
#include <queue>
#include <chrono>
#include <atomic>
#include <cstring>

using namespace std;

struct Packet
{
    char data[64];
};

queue<Packet> myQueue;
mutex myMutex;

atomic<bool> producerDone(false);

atomic<long long> pushCount(0);
atomic<long long> popCount(0);

void producerFunction()
{
    Packet p;
    memset(p.data, 1, sizeof(p.data));

    auto startTime = chrono::steady_clock::now();

    while (true)
    {
        auto now = chrono::steady_clock::now();
        auto elapsedMs = chrono::duration_cast<chrono::milliseconds>(now - startTime).count();

        if (elapsedMs >= 1000)
        {
            break;
        }
        {
            lock_guard<mutex> lock(myMutex);
            myQueue.push(p);
        }
        pushCount++;
    }

    producerDone = true;
}

void consumerFunction()
{
    while (true)
    {
        Packet p;
        bool gotSomething = false;

        {
            lock_guard<mutex> lock(myMutex);
            if (!myQueue.empty())
            {
                p = myQueue.front();
                myQueue.pop();
                gotSomething = true;
            }
        }

        if (gotSomething)
        {
            popCount++;
        }
        else
        {
            if (producerDone)
            {
                lock_guard<mutex> lock(myMutex);
                if (myQueue.empty())
                {
                    break;
                }
            }
        }
    }
}

int main()
{
    thread t1(producerFunction);
    thread t2(consumerFunction);

    t1.join();
    t2.join();

    cout << "pushed in 1 sec: " << pushCount << endl;
    cout << "popped in 1 sec: " << popCount << endl;

    return 0;
}
