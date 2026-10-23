# Simple build: g++ -O2, standard library only.
CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall

all: bench

bench: bench.cpp common.hpp book_a.hpp book_b.hpp book_c.hpp
	$(CXX) $(CXXFLAGS) -o bench bench.cpp

verify: bench
	./bench --verify

# 5 runs per implementation, same workload (fixed seed 42).
measure: bench
	for i in A B C; do \
		echo "=== impl $$i ==="; \
		for r in 1 2 3 4 5; do ./bench --impl $$i | grep -E '^(total_ms|add:|cancel:|bbo:)'; echo "---"; done; \
	done

clean:
	rm -f bench
