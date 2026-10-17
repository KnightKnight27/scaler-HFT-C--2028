CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic

SRCS = order_book.cpp map_order_book.cpp ring_order_book.cpp bench.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = book

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

verify: $(TARGET)
	./$(TARGET) --verify

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all verify clean
