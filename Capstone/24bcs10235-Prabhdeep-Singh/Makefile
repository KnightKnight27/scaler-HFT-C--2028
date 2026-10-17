CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

bench: bench.cpp orderbook.h impl_a.h impl_b.h impl_c.h
	$(CXX) $(CXXFLAGS) -o bench bench.cpp

clean:
	rm -f bench perf.data perf.data.old
