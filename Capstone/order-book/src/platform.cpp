#include "platform.hpp"

#include <sys/resource.h>

#if defined(__APPLE__)
#include <libproc.h>
#include <unistd.h>
#endif

ProcessCounters read_process_counters() {
    ProcessCounters counters;

    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    counters.minor_page_faults = usage.ru_minflt;
    counters.major_page_faults = usage.ru_majflt;

#if defined(__APPLE__)
    rusage_info_v4 info{};
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&info)) == 0) {
        counters.has_cpu_counters = true;
        counters.cycles = info.ri_cycles;
        counters.instructions = info.ri_instructions;
    }
#endif
    return counters;
}

double peak_rss_mib() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
    return usage.ru_maxrss / (1024.0 * 1024.0);  // bytes on macOS
#else
    return usage.ru_maxrss / 1024.0;  // KiB on Linux
#endif
}
