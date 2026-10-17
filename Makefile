CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic -Werror
LDLIBS := -pthread

.PHONY: all test benchmark sanitize tsan clean
all: build/spsc_queue build/spsc_queue_test

build:
	mkdir -p build

build/spsc_queue: spsc_queue.cpp spsc_queue.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

build/spsc_queue_test: spsc_queue_test.cpp spsc_queue.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

test: build/spsc_queue_test
	timeout 60s ./build/spsc_queue_test

benchmark: build/spsc_queue
	./build/spsc_queue mutex 1 1024 5
	./build/spsc_queue spin 1 1024 5

sanitize: | build
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer spsc_queue_test.cpp -o build/spsc_queue_test_asan $(LDLIBS)
	timeout 60s ./build/spsc_queue_test_asan

tsan: | build
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=thread spsc_queue_test.cpp -o build/spsc_queue_test_tsan $(LDLIBS)
	timeout 60s ./build/spsc_queue_test_tsan

clean:
	rm -rf build
