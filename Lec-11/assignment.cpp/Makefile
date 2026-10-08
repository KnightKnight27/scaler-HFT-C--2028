CXX=g++
FLAGS=-O2 -pthread -Wall

all: test bench bench_mutex naive

test: test_ringbuffer.cpp ringbuffer.h
	$(CXX) $(FLAGS) test_ringbuffer.cpp -o test

bench: bench.cpp spsc_queue.h ringbuffer.h spinlock.h mutex.h
	$(CXX) $(FLAGS) bench.cpp -o bench

bench_mutex: bench_mutex.cpp mutex_queue.h ringbuffer.h
	$(CXX) $(FLAGS) bench_mutex.cpp -o bench_mutex

latency: latency_bench.cpp spsc_queue.h ringbuffer.h spinlock.h mutex.h
	$(CXX) $(FLAGS) latency_bench.cpp -o latency

# -O0 on purpose, with -O2 the naive lock can hang forever
naive: naive_test.cpp spinlock.h
	$(CXX) -O0 -pthread naive_test.cpp -o naive

clean:
	rm -f test bench bench_mutex naive