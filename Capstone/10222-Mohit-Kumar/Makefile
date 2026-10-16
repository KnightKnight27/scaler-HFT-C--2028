CXX ?= c++
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic
HEADERS := $(wildcard include/orderbook/*.hpp)

.PHONY: all test sanitize clean
all: build/bench build/verify

build:
	mkdir -p build
build/bench: src/bench.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/verify: tests/verify.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
test: build/verify
	./build/verify
build/verify-sanitize: tests/verify.cpp $(HEADERS) | build
	clang++ $(CPPFLAGS) -std=c++17 -O1 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -fsanitize=address,undefined $< -o $@
sanitize: build/verify-sanitize
	./build/verify-sanitize 20000 42
clean:
	rm -rf build
