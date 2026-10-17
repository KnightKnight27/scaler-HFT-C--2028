CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INC      := -Iinclude
HDRS     := $(wildcard include/*.hpp)

# bench      : the graded build (-O2)
# bench_prof : same code with frame pointers + debug info, for `perf record -g`
all: bench

bench: src/bench.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) $(INC) $< -o $@

bench_prof: src/bench.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -g -fno-omit-frame-pointer $(INC) $< -o $@

verify: bench
	./bench --verify

clean:
	rm -f bench bench_prof perf.data perf.data.old cachegrind.out.*

.PHONY: all verify clean
