CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -g -fno-omit-frame-pointer

bench: bench.cpp common.hpp map_book.hpp heap_book.hpp array_book.hpp
	$(CXX) $(CXXFLAGS) -o bench bench.cpp

clean:
	rm -f bench perf.data perf.data.old

.PHONY: clean
