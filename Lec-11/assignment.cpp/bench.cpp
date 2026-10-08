#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include "spinlock.h"
#include "mutex.h"
#include "spsc_queue.h"

// 64 byte object
struct obj {
  long long id;
  char pad[56];
};
static_assert(sizeof(obj)==64, "obj shld be 64 bytes");

std::atomic<bool> stop{false};

template<typename L>
long long run(const char* name){
  static spsc_queue<obj,1024,L> q;  // static so its not on stack (64KB)
  long long pushed=0, popped=0;
  bool bad=false;
  stop=false;

  // producer
  std::thread t1([&](){
	obj o;
	while(!stop){
	  o.id=pushed;
	  if(q.push(o)) pushed++;
	}
  });

  // consumer
  std::thread t2([&](){
	obj o;
	while(!stop){
	  if(q.pop(o)){
		if(o.id!=popped) bad=true;  // order check
		popped++;
	  }
	}
  });

  std::this_thread::sleep_for(std::chrono::seconds(1));
  stop=true;
  t1.join();
  t2.join();

  // empty whatever is left so next run starts clean
  obj tmp;
  while(q.pop(tmp)){}

  std::cout<<name<<" pushed: "<<pushed<<"  popped: "<<popped<<" /sec";
  if(bad) std::cout<<"  ORDER WRONG!!";
  std::cout<<"\n";
  return popped;
}

int main(){
  // run few times, first run is usually slower
  for(int i=0;i<5;i++){
	std::cout<<"run "<<i+1<<"\n";
	run<spinlock>("  spinlock ");
	run<mymutex>("  mymutex  ");
	run<std::mutex>("  mutex    ");
  }
}