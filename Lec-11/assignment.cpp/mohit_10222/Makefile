CXX = clang++
COMMON = -std=c++17 -pthread -Wall -Wextra -Wpedantic -Werror
RELEASE = -O3 -DNDEBUG
HEADERS = spsc_queue.hpp object64.hpp

.PHONY: all test benchmark asan tsan clean

all: build/spsc_bench build/spsc_tests

build:
	mkdir -p build

build/spsc_bench: spsc_queue.cpp $(HEADERS) Makefile | build
	$(CXX) $(COMMON) $(RELEASE) spsc_queue.cpp -o $@

build/spsc_tests: tests/test_spsc.cpp $(HEADERS) Makefile | build
	$(CXX) $(COMMON) -O2 tests/test_spsc.cpp -o $@

test: build/spsc_tests build/spsc_bench
	./build/spsc_tests
	python3 tests/test_cli.py ./build/spsc_bench

benchmark: build/spsc_bench
	./build/spsc_bench 1 5 1024 both

build/spsc_tests_asan: tests/test_spsc.cpp $(HEADERS) Makefile | build
	$(CXX) $(COMMON) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_spsc.cpp -o $@

build/spsc_bench_asan: spsc_queue.cpp $(HEADERS) Makefile | build
	$(CXX) $(COMMON) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined spsc_queue.cpp -o $@

asan: build/spsc_tests_asan build/spsc_bench_asan
	./build/spsc_tests_asan
	python3 tests/test_cli.py ./build/spsc_bench_asan

build/spsc_tests_tsan: tests/test_spsc.cpp $(HEADERS) Makefile | build
	$(CXX) $(COMMON) -O1 -g -fno-omit-frame-pointer -fsanitize=thread tests/test_spsc.cpp -o $@

build/spsc_bench_tsan: spsc_queue.cpp $(HEADERS) Makefile | build
	$(CXX) $(COMMON) -O1 -g -fno-omit-frame-pointer -fsanitize=thread spsc_queue.cpp -o $@

tsan: build/spsc_tests_tsan build/spsc_bench_tsan
	./build/spsc_tests_tsan
	python3 tests/test_cli.py ./build/spsc_bench_tsan

clean:
	rm -rf build
