#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include "mutex_queue.h"

struct obj {
  long long id;
  char pad[56];
};
static_assert(sizeof(obj)==64, "obj shld be 64 bytes");

std::atomic<bool> stop{false};
mutex_queue<obj,1024> q;

int main(){
  for(int r=0;r<5;r++){
	long long pushed=0, popped=0;
	bool bad=false;
	stop=false;

	std::thread t1([&](){
	  obj o;
	  while(!stop){
		o.id=pushed;
		if(q.push(o)) pushed++;
	  }
	});

	std::thread t2([&](){
	  obj o;
	  while(!stop){
		if(q.pop(o)){
		  if(o.id!=popped) bad=true;
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

	std::cout<<"run "<<r+1<<" mutex  pushed: "<<pushed<<"  popped: "<<popped<<" /sec";
	if(bad) std::cout<<"  ORDER WRONG!!";
	std::cout<<"\n";
  }
}