# make            builds ./bench and ./tests with -O2
# make test       unit tests
# make verify     correctness test (all three implementations, same workload)
# make bench-all  benchmark A, B and C, 5 runs each
# override the compiler with: make CXX=clang++
CXX      = g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -g
CXXFLAGS += $(CXXFLAGS_EXTRA)
BINDIR   ?= .

BOOKS   := src/map_book.cpp src/heap_book.cpp src/array_book.cpp
HEADERS := include/order_book.h include/map_book.h include/heap_book.h include/array_book.h \
           src/workload.h src/reference_book.h

all: $(BINDIR)/bench $(BINDIR)/tests

$(BINDIR)/bench: src/bench.cpp $(BOOKS) $(HEADERS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -Iinclude src/bench.cpp $(BOOKS) -o $@

$(BINDIR)/tests: src/tests.cpp $(BOOKS) $(HEADERS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -Iinclude src/tests.cpp $(BOOKS) -o $@

test: $(BINDIR)/tests
	$(BINDIR)/tests

verify: $(BINDIR)/bench
	$(BINDIR)/bench --verify

bench-all: $(BINDIR)/bench
	$(BINDIR)/bench --impl A --repeat 5
	$(BINDIR)/bench --impl B --repeat 5
	$(BINDIR)/bench --impl C --repeat 5

clean:
	rm -f $(BINDIR)/bench $(BINDIR)/tests
	rm -rf $(BINDIR)/bench.dSYM $(BINDIR)/tests.dSYM

.PHONY: all test verify bench-all clean
