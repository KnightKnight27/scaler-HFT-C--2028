CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

bench: bench.cpp book_a.hpp book_b.hpp book_c.hpp common.hpp workload.hpp
	$(CXX) $(CXXFLAGS) bench.cpp -o bench

verify: bench
	./bench --verify

run: bench
	./bench --impl all

perf: bench          # Linux only; writes results/
	./scripts/run_perf.sh

clean:
	rm -rf bench bench_prof build
.PHONY: verify run perf clean
