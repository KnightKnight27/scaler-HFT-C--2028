
CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude

VERIFY = verify
BENCH = bench

COMMON_SRC = src/map_book.cpp src/heap_book.cpp src/array_book.cpp src/segment_tree_book.cpp

.PHONY: all clean test

all: $(VERIFY) $(BENCH)

$(VERIFY): $(COMMON_SRC) tests/verify.cpp include/order_book.hpp include/map_book.hpp include/heap_book.hpp include/array_book.hpp include/segment_tree_book.hpp
	$(CXX) $(CXXFLAGS) $(COMMON_SRC) tests/verify.cpp -o $(VERIFY)

$(BENCH): $(COMMON_SRC) bench.cpp include/order_book.hpp include/map_book.hpp include/heap_book.hpp include/array_book.hpp include/segment_tree_book.hpp
	$(CXX) $(CXXFLAGS) $(COMMON_SRC) bench.cpp -o $(BENCH)

test: $(VERIFY)
	./$(VERIFY)

clean:
	rm -f $(VERIFY) $(BENCH)
