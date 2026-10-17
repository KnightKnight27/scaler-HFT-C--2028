CXX := clang++
CXXFLAGS := -std=c++17 -O3 -march=native -Wall -Wextra -pthread

all: bench_all test_queue

bench_all: benchmark.cpp spsc_queue.hpp
	$(CXX) $(CXXFLAGS) benchmark.cpp -o bench_all

test_queue: main.cpp spsc_queue.hpp
	$(CXX) $(CXXFLAGS) main.cpp -o test_queue

run: bench_all
	./bench_all

clean:
	rm -f bench_all test_queue
