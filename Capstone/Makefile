CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic -Iinclude

SRCS = src/OrderBookA.cpp src/OrderBookB.cpp src/OrderBookC.cpp src/bench.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = bench
UNIT_TESTS = unit_tests
STRESS_TEST = stress_test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(UNIT_TESTS): src/OrderBookA.cpp src/OrderBookB.cpp src/OrderBookC.cpp src/unit_tests.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

$(STRESS_TEST): src/OrderBookA.cpp src/OrderBookB.cpp src/OrderBookC.cpp src/stress_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

verify: $(TARGET)
	./$(TARGET) --verify

test: $(UNIT_TESTS) $(STRESS_TEST)
	./$(UNIT_TESTS)
	./$(STRESS_TEST)

bench-a: $(TARGET)
	./$(TARGET) --impl A

bench-b: $(TARGET)
	./$(TARGET) --impl B

bench-c: $(TARGET)
	./$(TARGET) --impl C

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET).exe $(UNIT_TESTS) $(UNIT_TESTS).exe $(STRESS_TEST) $(STRESS_TEST).exe

.PHONY: all clean verify test bench-a bench-b bench-c
