CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude

SRCS = src/OrderBookA.cpp src/OrderBookB.cpp src/OrderBookC.cpp
BENCH_SRCS = $(SRCS) src/bench.cpp
TEST_SRCS = $(SRCS) src/test.cpp

all: bench test_suite

bench: $(BENCH_SRCS)
	$(CXX) $(CXXFLAGS) -o bench $(BENCH_SRCS)

test_suite: $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o test_suite $(TEST_SRCS)

verify: bench
	./bench --verify

clean:
	rm -f bench test_suite *.o

.PHONY: all clean verify
