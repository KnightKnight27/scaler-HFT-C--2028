CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude

all: bench test_a test_b test_c

bench: src/bench.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) src/bench.cpp -o bench

test_a: src/order_book_first.cpp include/order_book_first.hpp include/order_book_common.hpp
	$(CXX) $(CXXFLAGS) src/order_book_first.cpp -o test_a

test_b: src/order_book_second.cpp include/order_book_second.hpp include/order_book_common.hpp
	$(CXX) $(CXXFLAGS) src/order_book_second.cpp -o test_b

test_c: src/order_book_third.cpp include/order_book_third.hpp include/order_book_common.hpp
	$(CXX) $(CXXFLAGS) src/order_book_third.cpp -o test_c

test: test_a test_b test_c
	./test_a
	./test_b
	./test_c

verify: bench
	./bench --verify

clean:
	rm -f bench test_a test_b test_c *.exe perf.data perf.data.old

.PHONY: all test verify clean
