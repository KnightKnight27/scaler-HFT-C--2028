CXX = g++
CXXFLAGS = -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic

bench: bench.cpp order_book.cpp order_book.h
	$(CXX) $(CXXFLAGS) bench.cpp order_book.cpp -o bench

verify: bench
	./bench --verify

clean:
	rm -f bench bench-sanitize

.PHONY: verify clean
