CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

bench: bench.cpp common.hpp order_books.hpp
	$(CXX) $(CXXFLAGS) bench.cpp -o bench

clean:
	rm -f bench

.PHONY: clean
