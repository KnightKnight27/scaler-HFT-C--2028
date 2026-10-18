CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INCLUDES  = -Iinclude -Isrc
SOURCES   = src/bench.cpp
HEADERS   = $(wildcard include/*.hpp src/*.hpp)

all: bench bench_prof

bench: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) -g $(INCLUDES) $(SOURCES) -o $@

# Frame pointers, so perf record -g gets usable call graphs.
bench_prof: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) -g -fno-omit-frame-pointer $(INCLUDES) $(SOURCES) -o $@

verify: bench
	./bench --verify

clean:
	rm -f bench bench_prof perf.data perf.data.old

.PHONY: all verify clean
