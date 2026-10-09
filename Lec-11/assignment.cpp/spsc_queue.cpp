#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>

using namespace std;
using namespace std::chrono;

// one item is exactly bytes i.e = one cache line
struct alignas(64) Item {
	uint64_t seq; //8 bytes , a counter so we can check the order later
	char pad[56]; //56bytes of filler -> 8+56 = 64
};
static_assert(sizeof(Item) == 64, "Item must be 64 bytes");
const size_t CAP = 1024; //how many slots the pool has 

//MY own spinlock
class SpinLock{
	atomic_flag flag = ATOMIC_FLAG_INIT ; //fasle = unlocked
public:
	void lock(){
		//test_ and_set: sets flag to true, returns what itwas before
		//if it was already true, someone else has th elock -> keep looping
		while (flag.test_and_set(memory_order_acquire)){}
	}
	
	void unlock(){
		flag.clear(memory_order_release); //set back to false
	}
};

//the queue, LockType is the blank in the template 
template <typename LockType>
class Queue {
	Item pool[CAP]; // memory pool, allocated once, reused forever
	size_t head = 0; //where the consumeer reads next
	size_t tail = 0; //where the producer writes next
	size_t count = 0; //how many items are inside rn
	LockType lk; //the lock (spinlokc or mutex)


public:
	bool push(const Item& it){
		lock_guard<LockType> g(lk); //locks now , unnlock automatically at the closing brace
		if(count == CAP) return false; //full, cannot push
		pool[tail] = it; //copy item into the lost
		tail = (tail + 1) % CAP; //move tail forward, wrap to 0 at the end
		count++;
		return true;
	}

	bool pop(Item& out) {
		lock_guard<LockType> g(lk);
		if(count == 0) return false; // empty, nothing to pop
		out = pool[head]; //copy slot out
		head = (head+1) % CAP;
		count--;
		return true;
	}
};

template <typename LockType>
void bench(const string& name) {
	Queue<LockType>* q = new Queue<LockType>(); //heap,  because 64kB is big for the stack
	atomic<bool> stop(false);
	uint64_t pushed =0, popped=0;
	bool order_ok = true;

	//producer
	thread t1([&]() {
		Item it{};
		uint64_t n =0;
		while (!stop.load()){
			it.seq = n;
			if (q->push(it)) n++; //only count if push actually worked 
		}
		pushed = n;
	});

	//consumer
	thread t2([&](){
		Item it;
		uint64_t expected =0;
		while(!stop.load()) {
			if(q->pop(it)){
				if(it.seq != expected) order_ok = false; //FIFO check
				expected++;
			}
		}
		popped = expected;
	});
	
	auto start = steady_clock::now();
	this_thread::sleep_for(seconds(1)); //main sleeps, threads work
	stop.store(true);
	t1.join();
	t2.join();
	auto end = steady_clock::now();

	double secs = duration<double>(end - start).count();
	cout << name << ":\n";
	cout <<" pushed = " << pushed <<"\n";
	cout <<"popped = " << popped <<"\n";
	cout << "  ops/sec = " << (uint64_t)(popped / secs) << "\n";
	cout << "  order ok = " << (order_ok ? "yes" : "NO") << "\n";
	
	delete q;
}

int main(){
	bench<SpinLock>("spinlock");
	bench<mutex>("std::mutex");
	return 0;
}
