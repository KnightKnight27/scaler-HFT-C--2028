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

// single file version, everything inline so it can be submitted on its own

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstddef>

// 64 byte object
struct obj {
  long long id;
  char pad[56];
};
static_assert(sizeof(obj)==64, "obj shld be 64 bytes");

// ring buffer is the memory pool: all slots preallocated, nothing gets
// new'ed or deleted while pushing/popping
// head and tail only go up, real index is %N
// empty -> tail==head, full -> tail-head==N
// NOT thread safe on its own, lock goes outside
template<typename T, size_t N>
class RingBuffer {
  T buf[N];
  size_t head = 0;
  size_t tail = 0;

public:
  bool push(const T& v){
	if(tail-head==N) return false;
	buf[tail%N]=v;
	tail++;
	return true;
  }

  bool pop(T& out){
	if(tail==head) return false;
	out=buf[head%N];
	head++;
	return true;
  }
};

// basic spinlock
// f is false when nobody has the lock, true when someone does
struct spinlock {
  std::atomic<bool> f{false};
  void lock(){ while(f.exchange(true)){} }
  void unlock(){ f=false; }
};

// own mutex: yield the cpu instead of burning it while waiting
struct mymutex {
  std::atomic<bool> f{false};
  void lock(){ while(f.exchange(true)) std::this_thread::yield(); }
  void unlock(){ f=false; }
};

// ringbuffer with a lock around it
// L can be spinlock, mymutex or std::mutex, all have lock() and unlock()
template<typename T, size_t N, typename L>
class spsc_queue {
  RingBuffer<T,N> rb;
  L l;

public:
  bool push(const T& v){
	std::lock_guard<L> g(l);
	return rb.push(v);
  }

  bool pop(T& out){
	std::lock_guard<L> g(l);
	return rb.pop(out);
  }
};

// same ringbuffer but no lock at all, just atomic head/tail
// works bcz spsc: only producer writes tail, only consumer writes head
// just to see how much the lock itself costs
template<typename T, size_t N>
class spsc_lockfree {
  T buf[N];
  std::atomic<size_t> head{0};
  std::atomic<size_t> tail{0};

public:
  bool push(const T& v){
	size_t t=tail.load();
	if(t-head.load()==N) return false;
	buf[t%N]=v;
	tail.store(t+1);
	return true;
  }

  bool pop(T& out){
	size_t h=head.load();
	if(h==tail.load()) return false;
	out=buf[h%N];
	head.store(h+1);
	return true;
  }
};

std::atomic<bool> stop{false};

// one 1-second run: t1 pushes, t2 pops, count how many got through
template<typename Q>
void run(const char* name){
  static Q q;   // static so its not on stack
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
}

int main(){
  // run few times, first run is usually slower
  for(int i=0;i<5;i++){
	std::cout<<"run "<<i+1<<"\n";
	run<spsc_queue<obj,1024,spinlock>>("  spinlock   ");
	run<spsc_queue<obj,1024,mymutex>>("  mymutex    ");
	run<spsc_queue<obj,1024,std::mutex>>("  std::mutex ");
	run<spsc_lockfree<obj,1024>>("  lockfree   ");
  }
}
