CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

IMPL_SRCS = OrderBookA.cpp OrderBookB.cpp OrderBookC.cpp
IMPL_HDRS = OrderBookA.h OrderBookB.h OrderBookC.h Common.h

all: demo bench

demo: main.cpp $(IMPL_SRCS) $(IMPL_HDRS)
	$(CXX) $(CXXFLAGS) main.cpp $(IMPL_SRCS) -o demo

bench: bench.cpp $(IMPL_SRCS) $(IMPL_HDRS)
	$(CXX) $(CXXFLAGS) bench.cpp $(IMPL_SRCS) -o bench

clean:
	rm -f demo bench

.PHONY: all clean
