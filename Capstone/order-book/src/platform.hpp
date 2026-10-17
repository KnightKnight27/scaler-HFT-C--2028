#pragma once

#include <cstdint>

// Counters the operating system keeps for this process. They are read before
// and after a benchmark pass, so the difference covers only that pass.
struct ProcessCounters {
    // Cycles and instructions retired. macOS exposes them to any process through
    // proc_pid_rusage; on Linux use perf stat instead.
    bool has_cpu_counters = false;
    std::uint64_t cycles = 0;
    std::uint64_t instructions = 0;

    long minor_page_faults = 0;  // served from memory (first touch of a new page)
    long major_page_faults = 0;  // needed disk I/O
};

ProcessCounters read_process_counters();

// Highest resident memory of this process so far, in MiB.
double peak_rss_mib();
