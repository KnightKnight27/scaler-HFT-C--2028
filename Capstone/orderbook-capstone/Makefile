CXX      := g++
CXXFLAGS := -O3 -g -std=c++17 -march=native -Wall -Wextra -Iinclude
TARGET   := bench

SRCS     := src/orderbook_a.cpp src/orderbook_b.cpp src/orderbook_c.cpp src/workload.cpp src/main.cpp
OBJS     := $(SRCS:.cpp=.o)

WSL_PERF := $(shell find /usr/lib/linux-tools* -name perf 2>/dev/null | head -n 1)
ifneq ($(WSL_PERF),)
    PERF := $(WSL_PERF)
else
    PERF := perf
endif

PERF_EVENTS := task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

.PHONY: all clean build verify run-bench perf results

all: build

build: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

verify: build
	@mkdir -p result
	./$(TARGET) --verify | tee result/correctness.txt

run-bench: build
	@mkdir -p result
	@rm -f result/benchmark.txt
	./$(TARGET) --impl A | tee -a result/benchmark.txt
	./$(TARGET) --impl B | tee -a result/benchmark.txt
	./$(TARGET) --impl C | tee -a result/benchmark.txt

perf: build
	@mkdir -p result
	@echo "Using perf binary: $(PERF)"
	@rm -f result/perf_stat.txt result/perf_record.txt
	@for impl in A B C; do \
		echo "=== PERF STAT IMPLEMENTATION $$impl ===" | tee -a result/perf_stat.txt; \
		$(PERF) stat -r 5 -e $(PERF_EVENTS) ./$(TARGET) --impl $$impl >> result/perf_stat.txt 2>&1; \
		echo "" >> result/perf_stat.txt; \
		echo "=== PERF RECORD / REPORT IMPLEMENTATION $$impl ===" | tee -a result/perf_record.txt; \
		$(PERF) record -e task-clock -F 999 -o result/perf_$$impl.data ./$(TARGET) --impl $$impl >/dev/null 2>&1; \
		$(PERF) report --stdio --no-children -i result/perf_$$impl.data --percent-limit 0.5 < /dev/null >> result/perf_record.txt 2>&1; \
		echo "" >> result/perf_record.txt; \
	done
	@rm -f result/perf_*.data*

results: verify run-bench perf

clean:
	rm -f $(OBJS) $(TARGET) result/perf_*.data*
