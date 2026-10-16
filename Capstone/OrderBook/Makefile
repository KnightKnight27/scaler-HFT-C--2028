CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude

SRCS = src/orderBookMap.cpp src/orderBookHeap.cpp src/orderBookArray.cpp src/main.cpp
TARGET = bench

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

verify: $(TARGET)
	./$(TARGET) --verify

clean:
	rm -f $(TARGET)

.PHONY: all verify clean
