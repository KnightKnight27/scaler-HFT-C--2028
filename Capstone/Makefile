CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

TARGET = bench

SRCS = bench.cpp
HDRS = common.hpp map_order_bk.hpp pq_order_bk.hpp fixed_order_bk.hpp

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all clean