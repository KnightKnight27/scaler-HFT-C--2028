CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -fno-omit-frame-pointer

bench: src/bench.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) -Iinclude src/bench.cpp -o bench

.PHONY: verify clean
verify: bench
	./bench --verify

clean:
	rm -f bench perf.data perf.data.old
