/*
 * perf_stat - A lightweight perf stat wrapper using perf_event_open syscall.
 * Usage: ./perf_stat [command] [args...]
 * Collects: cycles, instructions, branches, branch-misses, cache-misses,
 *           L1-dcache-load-misses, dTLB-load-misses, task-clock, page-faults
 *
 * Note: This is a simplified replacement for `perf stat` that works by using
 *       the perf_event_open syscall directly. It monitors child processes
 *       by using the inherit flag and reading counters after the child exits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <signal.h>
#include <linux/perf_event.h>
#include <fcntl.h>
#include <stdint.h>

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

struct perf_counter {
    const char *name;
    int fd;
    long long count;
    int available;
};

static int setup_counter(struct perf_counter *c, int type, uint64_t config, int inherit) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = type;
    pe.size = sizeof(pe);
    pe.config = config;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    if (inherit) {
        pe.inherit = 1;
        pe.inherit_stat = 1;
    }

    c->fd = perf_event_open(&pe, 0, -1, -1, 0);
    if (c->fd == -1) {
        c->fd = -1;
        c->available = 0;
        return -1;
    }
    c->available = 1;
    return 0;
}

static int setup_cache_counter(struct perf_counter *c, int cache_type, int cache_op, int result) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.size = sizeof(pe);
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.type = PERF_TYPE_HW_CACHE;
    pe.config = (cache_type << 16) | (cache_op << 8) | result;
    pe.inherit = 1;
    pe.inherit_stat = 1;

    c->fd = perf_event_open(&pe, 0, -1, -1, 0);
    if (c->fd == -1) {
        c->fd = -1;
        c->available = 0;
        return -1;
    }
    c->available = 1;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [command] [args...]\n", argv[0]);
        return 1;
    }

    struct perf_counter counters[] = {
        {"task-clock", -1, 0, 0},
        {"cycles", -1, 0, 0},
        {"instructions", -1, 0, 0},
        {"branches", -1, 0, 0},
        {"branch-misses", -1, 0, 0},
        {"cache-misses", -1, 0, 0},
        {"L1-dcache-load-misses", -1, 0, 0},
        {"dTLB-load-misses", -1, 0, 0},
        {"page-faults", -1, 0, 0}
    };

    int num_counters = sizeof(counters) / sizeof(counters[0]);
    int inherit = 1;

    /* Setup hardware counters */
    setup_counter(&counters[1], PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, inherit);
    setup_counter(&counters[2], PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, inherit);
    setup_counter(&counters[3], PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS, inherit);
    setup_counter(&counters[4], PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES, inherit);
    setup_counter(&counters[5], PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES, inherit);

    /* Setup cache-level counters */
    setup_cache_counter(&counters[6], PERF_COUNT_HW_CACHE_L1D,
                        PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS);

    /* Setup TLB misses */
    setup_cache_counter(&counters[7], PERF_COUNT_HW_CACHE_DTLB,
                        PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS);

    /* Software events */
    setup_counter(&counters[0], PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK, inherit);
    setup_counter(&counters[8], PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, inherit);

    /* Enable all available counters */
    for (int i = 0; i < num_counters; i++) {
        if (counters[i].fd != -1) {
            ioctl(counters[i].fd, PERF_EVENT_IOC_RESET, 0);
            ioctl(counters[i].fd, PERF_EVENT_IOC_ENABLE, 0);
        }
    }

    /* Fork and exec */
    pid_t child = fork();
    if (child == 0) {
        /* Child: signal parent that we're ready to exec */
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        execvp(argv[1], &argv[1]);
        perror("execvp");
        exit(127);
    }

    /* Parent: wait for child */
    int status;
    waitpid(child, &status, 0);

    /* Disable and read counters */
    for (int i = 0; i < num_counters; i++) {
        if (counters[i].fd != -1) {
            ioctl(counters[i].fd, PERF_EVENT_IOC_DISABLE, 0);
            read(counters[i].fd, &counters[i].count, sizeof(counters[i].count));
            close(counters[i].fd);
        }
    }

    /* Print results */
    printf("\n Performance counter stats for '%s':\n\n", argv[1]);
    for (int i = 0; i < num_counters; i++) {
        if (counters[i].available) {
            printf("  %20lld %s\n", counters[i].count, counters[i].name);
        } else {
            printf("  %20s %s (unavailable)\n", "N/A", counters[i].name);
        }
    }

    /* Calculate IPC */
    long long cycles = counters[1].count;
    long long instructions = counters[2].count;
    if (counters[1].available && counters[2].available && cycles > 0) {
        printf("  %20s IPC\n", "\0");
        printf("  Instructions per cycle: %.2f\n", (double)instructions / (double)cycles);
    }
    printf("\n");

    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
