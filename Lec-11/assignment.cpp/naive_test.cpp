#include <iostream>
#include <thread>
#include "spinlock.h"

// 2 threads increment same counter under a lock
// if lock works counter shld be exactly 2*ITERS
#define ITERS 1000000
long long counter=0;

template<typename L>
void test(const char* name){
  L l;
  counter=0;
  auto work=[&](){
	for(int i=0;i<ITERS;i++){
	  l.lock();
	  counter++;
	  l.unlock();
	}
  };
  std::thread t1(work), t2(work);
  t1.join();
  t2.join();
  std::cout<<name<<" counter = "<<counter<<"  (expected "<<2LL*ITERS<<")\n";
}

int main(){
  test<spinlock>("spinlock ");
  test<naiveSpin_lock>("naive    ");  // wrong count, or hangs with -O2
}