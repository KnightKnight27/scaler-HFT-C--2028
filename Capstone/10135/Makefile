# Build the order book benchmark.
#
# A single translation unit pulls in the three header-only books, so a plain
# g++ command works too:
#
#   g++ -O2 -std=c++17 -Iinclude src/bench.cpp -o bench

CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Iinclude

bench: src/bench.cpp include/common.hpp include/book_map.hpp include/book_heap.hpp include/book_array.hpp
	$(CXX) $(CXXFLAGS) src/bench.cpp -o bench

.PHONY: verify test clean
verify: bench
	./bench --verify

test: bench
	./bench --test

clean:
	rm -f bench
