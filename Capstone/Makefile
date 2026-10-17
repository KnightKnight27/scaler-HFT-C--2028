CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra

TARGET = bench
SOURCES = bench.cpp
HEADERS = common.hpp order_book_map.hpp order_book_vector.hpp order_book_array.hpp

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
