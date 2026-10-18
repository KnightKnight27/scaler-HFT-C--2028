CXX = c++
CPPFLAGS = -Isrc -Itests
CXXFLAGS = -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic
SOURCES = src/bench.cpp tests/verify.cpp
HEADERS = $(wildcard src/*.hpp tests/*.hpp)

.PHONY: all verify sanitize clean
all: bench

bench: $(SOURCES) $(HEADERS) Makefile
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $@

verify: bench
	./bench --verify

sanitize: $(SOURCES) $(HEADERS) Makefile
	mkdir -p build
	$(CXX) $(CPPFLAGS) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o build/bench-sanitize
	./build/bench-sanitize --verify

clean:
	rm -f bench build/bench-sanitize

