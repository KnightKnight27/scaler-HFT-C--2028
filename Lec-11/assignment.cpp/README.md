# SPSC Queue Assignment

Name: Vansh Dobhal
Roll no: 10099

## What I did

Made a SPSC queue using std::mutex. It is a ring buffer of fixed size 1024.
One producer thread pushes 64 byte objects and one consumer thread pops them.
Both run for 1 second and then I join them (t1.join(), t2.join()) and print the count.

The buffer is a fixed array made once at the start, so there is no new/delete
happening while pushing and popping.

Each object has an id, consumer checks that ids come in the same order
they were pushed.

## How to run

```
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc
./spsc
```

## Result

Machine: Intel i5-13450HX, Linux, g++ -O2

Object size = 64 bytes

| Run | Pushed in 1 sec | Popped in 1 sec |
|-----|-----------------|-----------------|
| 1   | 5416691         | 5415680         |
| 2   | 5875764         | 5875763         |
| 3   | 5283668         | 5282654         |

So around 5.3 to 5.8 million objects per second with mutex.

Popped is a little less than pushed because when the 1 second ends
some objects are still left in the queue.
