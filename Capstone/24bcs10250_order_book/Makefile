CXX = g++
CXXFLAGS = -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra

bench: bench.cpp common.hpp map_book.hpp heap_book.hpp array_book.hpp
	$(CXX) $(CXXFLAGS) bench.cpp -o bench

clean:
	rm -f bench perf.data perf.data.old
