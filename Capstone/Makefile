CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -pedantic

all: bench

bench: bench.cpp order_book.hpp order_book_map.hpp order_book_vector.hpp order_book_direct.hpp
	$(CXX) $(CXXFLAGS) bench.cpp -o bench

clean:
	rm -f bench bench.exe
