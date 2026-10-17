CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra

bench: bench.cpp common.hpp book_map.hpp book_heap.hpp book_array.hpp
	$(CXX) $(CXXFLAGS) -o bench bench.cpp

verify: bench
	./bench --verify

clean:
	rm -f bench perf.data*

.PHONY: verify clean
