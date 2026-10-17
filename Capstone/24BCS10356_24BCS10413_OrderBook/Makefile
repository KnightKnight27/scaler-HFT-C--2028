CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -g -fno-omit-frame-pointer
# -g and -fno-omit-frame-pointer only add symbols / keep frame pointers so that
# `perf record -g` produces readable call graphs; codegen stays -O2.
SANFLAGS  = -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all
HEADERS   = include/*.hpp src/*.hpp

all: bench test_orderbook

bench: src/bench.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc src/bench.cpp -o bench

test_orderbook: tests/test_orderbook.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc tests/test_orderbook.cpp -o test_orderbook

verify: bench
	./bench --verify

# regression + differential tests, then the full lock-step verify
test: bench test_orderbook
	./test_orderbook
	./bench --verify

# the same under AddressSanitizer + UndefinedBehaviorSanitizer (verify shortened)
sanitize:
	$(CXX) $(SANFLAGS) -Iinclude -Isrc tests/test_orderbook.cpp -o test_orderbook_san
	$(CXX) $(SANFLAGS) -Iinclude -Isrc src/bench.cpp -o bench_san
	./test_orderbook_san
	./bench_san --verify --ops 200000

clean:
	rm -f bench test_orderbook test_orderbook_san bench_san
	rm -rf bench.dSYM

.PHONY: all verify test sanitize clean
