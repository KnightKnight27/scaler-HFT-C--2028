CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra -pthread

TARGET = bench
SOURCES = bench.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean