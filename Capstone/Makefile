CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude

TARGET = bench
SRCS = src/main.cpp

all: $(TARGET)

$(TARGET): $(SRCS) include/*.hpp
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

verify: $(TARGET)
	./$(TARGET) --verify

bench-A: $(TARGET)
	./$(TARGET) --impl A

bench-B: $(TARGET)
	./$(TARGET) --impl B

bench-C: $(TARGET)
	./$(TARGET) --impl C

clean:
	rm -f $(TARGET)

.PHONY: all clean verify bench-A bench-B bench-C
