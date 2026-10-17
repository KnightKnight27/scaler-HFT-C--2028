CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic
SOURCES = bench.cpp workload.cpp verification.cpp implementation/map_order_book.cpp implementation/vector_order_book.cpp implementation/array_order_book.cpp
HEADERS = $(wildcard *.hpp implementation/*.hpp)

.PHONY: all verify clean
all: bench
bench: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@
verify: bench
	./bench --verify
clean:
	rm -f bench
