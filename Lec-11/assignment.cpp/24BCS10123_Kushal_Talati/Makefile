CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread

spsc_queue: spsc_queue.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

run: spsc_queue
	./spsc_queue 5 4096

clean:
	rm -f spsc_queue
