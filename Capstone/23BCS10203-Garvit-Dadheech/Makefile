# make          -> build/bench  (g++ -O2 -std=c++17)
# make verify   -> ./build/bench --verify
# make run      -> ./build/bench --impl A, B, C
# make perf     -> scripts/run_perf.sh (Linux only)
#
# or just:  g++ -O2 -std=c++17 src/bench.cpp -o bench
CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
BUILD    ?= build
BIN       = $(BUILD)/bench
SRC       = src/bench.cpp
HDRS      = src/common.hpp src/workload.hpp src/runner.hpp src/book_map.hpp src/book_heap.hpp src/book_array.hpp

all: $(BIN)

$(BIN): $(SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

verify: $(BIN)
	$(BIN) --verify

run: $(BIN)
	$(BIN) --impl A
	$(BIN) --impl B
	$(BIN) --impl C

perf:
	./scripts/run_perf.sh

clean:
	rm -rf $(BUILD) perf*.data*

.PHONY: all verify run perf clean

# macOS only, hardware counters through kperf (run the binary with sudo)
apple-pmu: tools/apple_pmu.cpp $(HDRS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) tools/apple_pmu.cpp -o $(BUILD)/apple_pmu

.PHONY: apple-pmu
