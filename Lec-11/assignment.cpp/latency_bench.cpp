#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <algorithm>
#include "spinlock.h"
#include "mutex.h"
#include "spsc_queue.h"

#define LOOPS 10000000   // for lock tests
#define MSGS 100000      // for queue test

long long now_ns(){
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
	std::chrono::steady_clock::now().time_since_epoch()).count();
}

// still 64 bytes, but now carries the time it was pushed
struct obj {
  long long id;
  long long t;
  char pad[48];
};
static_assert(sizeof(obj)==64, "obj shld be 64 bytes");

// 1. only one thread, nobody else wants the lock
template<typename L>
void uncontended(const char* name){
  L l;
  long long s=now_ns();
  for(int i=0;i<LOOPS;i++){
	l.lock();
	l.unlock();
  }
  long long e=now_ns();
  std::cout<<name<<"  "<<(double)(e-s)/LOOPS<<" ns per lock+unlock\n";
}

// 2. two threads fighting for the same lock
template<typename L>
void contended(const char* name){
  L l;
  long long counter=0;
  auto work=[&](){
	for(int i=0;i<LOOPS/2;i++){
	  l.lock();
	  counter++;
	  l.unlock();
	}
  };
  long long s=now_ns();
  std::thread t1(work), t2(work);
  t1.join();
  t2.join();
  long long e=now_ns();
  std::cout<<name<<"  "<<(double)(e-s)/LOOPS<<" ns per lock+unlock";
  if(counter!=LOOPS) std::cout<<"  COUNTER WRONG!!";
  std::cout<<"\n";
}

// 3. time from push to pop, one msg at a time
template<typename L>
void queue_latency(const char* name){
  static spsc_queue<obj,1024,L> q;
  std::vector<long long> lat(MSGS);
  std::atomic<int> got{0};

  std::thread cons([&](){
	obj o;
	for(int i=0;i<MSGS;i++){
	  while(!q.pop(o)){}
	  lat[i]=now_ns()-o.t;
	  got.store(i+1, std::memory_order_release);
	}
  });

  std::thread prod([&](){
	obj o;
	for(int i=0;i<MSGS;i++){
	  o.id=i;
	  o.t=now_ns();
	  while(!q.push(o)){}
	  // wait till consumer got it, so queue never fills up
	  while(got.load(std::memory_order_acquire)!=i+1){}
	}
  });

  prod.join();
  cons.join();

  std::sort(lat.begin(), lat.end());
  long long sum=0;
  for(int i=0;i<MSGS;i++) sum+=lat[i];

  std::cout<<name<<"  avg: "<<sum/MSGS<<" ns"
	<<"  p50: "<<lat[MSGS/2]<<" ns"
	<<"  p99: "<<lat[MSGS*99/100]<<" ns"
	<<"  max: "<<lat[MSGS-1]<<" ns\n";
}

int main(){
  std::cout<<"--- lock latency, 1 thread (no contention) ---\n";
  uncontended<spinlock>("spinlock  ");
  uncontended<mymutex>("mymutex   ");
  uncontended<std::mutex>("std::mutex");

  std::cout<<"\n--- lock latency, 2 threads fighting ---\n";
  contended<spinlock>("spinlock  ");
  contended<mymutex>("mymutex   ");
  contended<std::mutex>("std::mutex");

  std::cout<<"\n--- spsc queue latency, push -> pop ---\n";
  queue_latency<spinlock>("spinlock  ");
  queue_latency<mymutex>("mymutex   ");
  queue_latency<std::mutex>("std::mutex");
}