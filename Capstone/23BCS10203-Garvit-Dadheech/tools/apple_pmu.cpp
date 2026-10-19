// Hardware counters on my Mac (Apple Silicon).  perf can't run on macOS and
// the docker VM has no PMU, so this uses the private kperf/kperfdata
// frameworks (what Instruments' CPU Counters uses) to read the M2 counters
// around the same loop that bench runs (src/runner.hpp).
//
//   make apple-pmu
//   sudo ./build/apple_pmu --impl A     (programming the PMU needs root)
//
// Counters are per thread (kpc_get_thread_counters) and only cover the op
// loop.  Page faults and CPU time come from getrusage().  Event names are
// from /usr/share/kpep/<cpu>.plist; there is no last level cache miss event,
// so cache-misses can't be reported.
#include <dlfcn.h>
#include <pthread.h>
#include <sys/resource.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../src/book_array.hpp"
#include "../src/book_heap.hpp"
#include "../src/book_map.hpp"
#include "../src/runner.hpp"
#include "../src/workload.hpp"

namespace {

// private framework functions, looked up with dlsym
constexpr uint32_t KPC_MAX_COUNTERS = 32;
using kpc_config_t = uint64_t;
struct kpep_db;
struct kpep_config;
struct kpep_event;

struct Kperf {
    int (*kpc_force_all_ctrs_set)(int) = nullptr;
    int (*kpc_set_counting)(uint32_t) = nullptr;
    int (*kpc_set_thread_counting)(uint32_t) = nullptr;
    int (*kpc_set_config)(uint32_t, kpc_config_t*) = nullptr;
    int (*kpc_get_thread_counters)(uint32_t, uint32_t, void*) = nullptr;

    int (*kpep_db_create)(const char*, kpep_db**) = nullptr;
    int (*kpep_db_event)(kpep_db*, const char*, kpep_event**) = nullptr;
    int (*kpep_config_create)(kpep_db*, kpep_config**) = nullptr;
    int (*kpep_config_force_counters)(kpep_config*) = nullptr;
    int (*kpep_config_add_event)(kpep_config*, kpep_event**, uint32_t, uint32_t*) = nullptr;
    int (*kpep_config_kpc)(kpep_config*, kpc_config_t*, size_t) = nullptr;
    int (*kpep_config_kpc_classes)(kpep_config*, uint32_t*) = nullptr;
    int (*kpep_config_kpc_count)(kpep_config*, size_t*) = nullptr;
    int (*kpep_config_kpc_map)(kpep_config*, size_t*, size_t) = nullptr;

    bool load() {
        void* kperf = dlopen("/System/Library/PrivateFrameworks/kperf.framework/kperf", RTLD_LAZY);
        void* kpd = dlopen("/System/Library/PrivateFrameworks/kperfdata.framework/kperfdata", RTLD_LAZY);
        if (!kperf || !kpd) {
            std::fprintf(stderr, "cannot load kperf/kperfdata frameworks: %s\n", dlerror());
            return false;
        }
        bool ok = true;
        auto bind = [&](void* lib, const char* name, auto& fn) {
            fn = reinterpret_cast<std::decay_t<decltype(fn)>>(dlsym(lib, name));
            if (!fn) { std::fprintf(stderr, "missing symbol %s\n", name); ok = false; }
        };
        bind(kperf, "kpc_force_all_ctrs_set", kpc_force_all_ctrs_set);
        bind(kperf, "kpc_set_counting", kpc_set_counting);
        bind(kperf, "kpc_set_thread_counting", kpc_set_thread_counting);
        bind(kperf, "kpc_set_config", kpc_set_config);
        bind(kperf, "kpc_get_thread_counters", kpc_get_thread_counters);
        bind(kpd, "kpep_db_create", kpep_db_create);
        bind(kpd, "kpep_db_event", kpep_db_event);
        bind(kpd, "kpep_config_create", kpep_config_create);
        bind(kpd, "kpep_config_force_counters", kpep_config_force_counters);
        bind(kpd, "kpep_config_add_event", kpep_config_add_event);
        bind(kpd, "kpep_config_kpc", kpep_config_kpc);
        bind(kpd, "kpep_config_kpc_classes", kpep_config_kpc_classes);
        bind(kpd, "kpep_config_kpc_count", kpep_config_kpc_count);
        bind(kpd, "kpep_config_kpc_map", kpep_config_kpc_map);
        return ok;
    }
};

struct EventSpec {
    const char* label;   // perf-stat style name used in the output
    const char* name;    // kpep event name
};

// two fixed counters, then up to 6 configurable ones (the M2 has 8)
const EventSpec kEvents[] = {
    {"cycles", "FIXED_CYCLES"},
    {"instructions", "FIXED_INSTRUCTIONS"},
    {"branches", "INST_BRANCH"},
    {"branch-misses", "BRANCH_MISPRED_NONSPEC"},
    {"L1-dcache-load-misses", "L1D_CACHE_MISS_LD"},
    {"L1-dcache-store-misses", "L1D_CACHE_MISS_ST"},
    {"dTLB-load-misses", "L1D_TLB_MISS"},
    {"L2-dTLB-misses", "L2_TLB_MISS_DATA"},
};
constexpr size_t kNumEvents = sizeof(kEvents) / sizeof(kEvents[0]);

struct Pmu {
    Kperf k;
    uint32_t classes = 0;
    size_t counter_map[KPC_MAX_COUNTERS] = {};

    bool setup() {
        if (!k.load()) return false;
        kpep_db* db = nullptr;
        if (k.kpep_db_create(nullptr, &db) != 0) { std::fprintf(stderr, "kpep_db_create failed\n"); return false; }
        kpep_config* cfg = nullptr;
        if (k.kpep_config_create(db, &cfg) != 0) { std::fprintf(stderr, "kpep_config_create failed\n"); return false; }
        if (k.kpep_config_force_counters(cfg) != 0) { std::fprintf(stderr, "kpep_config_force_counters failed\n"); return false; }
        for (const EventSpec& e : kEvents) {
            kpep_event* ev = nullptr;
            if (k.kpep_db_event(db, e.name, &ev) != 0) { std::fprintf(stderr, "unknown event %s\n", e.name); return false; }
            uint32_t err = 0;
            if (k.kpep_config_add_event(cfg, &ev, 0, &err) != 0) {
                std::fprintf(stderr, "cannot add event %s (err %u)\n", e.name, err);
                return false;
            }
        }
        size_t reg_count = 0;
        kpc_config_t regs[KPC_MAX_COUNTERS] = {};
        if (k.kpep_config_kpc_classes(cfg, &classes) != 0 ||
            k.kpep_config_kpc_count(cfg, &reg_count) != 0 ||
            k.kpep_config_kpc_map(cfg, counter_map, sizeof(counter_map)) != 0 ||
            k.kpep_config_kpc(cfg, regs, sizeof(regs)) != 0) {
            std::fprintf(stderr, "kpep_config_kpc_* failed\n");
            return false;
        }
        if (k.kpc_force_all_ctrs_set(1) != 0) {
            std::fprintf(stderr, "kpc_force_all_ctrs_set failed: this tool must run as root (sudo)\n");
            return false;
        }
        if ((classes & 2) && k.kpc_set_config(classes, regs) != 0) { std::fprintf(stderr, "kpc_set_config failed\n"); return false; }
        if (k.kpc_set_counting(classes) != 0) { std::fprintf(stderr, "kpc_set_counting failed\n"); return false; }
        if (k.kpc_set_thread_counting(classes) != 0) { std::fprintf(stderr, "kpc_set_thread_counting failed\n"); return false; }
        return true;
    }

    void read(uint64_t out[kNumEvents]) {
        uint64_t raw[KPC_MAX_COUNTERS] = {};
        if (k.kpc_get_thread_counters(0, KPC_MAX_COUNTERS, raw) != 0) std::fprintf(stderr, "kpc_get_thread_counters failed\n");
        for (size_t i = 0; i < kNumEvents; ++i) out[i] = raw[counter_map[i]];
    }
};

struct Sample {
    uint64_t ev[kNumEvents] = {};
    long minflt = 0, majflt = 0;
    double task_ms = 0;   // user + system CPU time
    double wall_ms = 0;
    uint64_t checksum = 0;
};

template <class Book>
Sample measure(Pmu& pmu, const std::vector<ob::Op>& ops) {
    uint64_t before[kNumEvents], after[kNumEvents];
    rusage ru0{}, ru1{};
    getrusage(RUSAGE_SELF, &ru0);
    pmu.read(before);
    const ob::RunResult r = ob::run_once<Book, false>(ops);
    pmu.read(after);
    getrusage(RUSAGE_SELF, &ru1);

    Sample s;
    for (size_t i = 0; i < kNumEvents; ++i) s.ev[i] = after[i] - before[i];
    s.minflt = ru1.ru_minflt - ru0.ru_minflt;
    s.majflt = ru1.ru_majflt - ru0.ru_majflt;
    auto ms = [](timeval a, timeval b) { return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_usec - a.tv_usec) / 1e3; };
    s.task_ms = ms(ru0.ru_utime, ru1.ru_utime) + ms(ru0.ru_stime, ru1.ru_stime);
    s.wall_ms = r.total_ms;
    s.checksum = r.checksum;
    return s;
}

template <class T>
T median_of(std::vector<T> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Book>
int run(Pmu& pmu, const std::vector<ob::Op>& ops, int reps, const char* impl) {
    std::vector<Sample> samples;
    for (int i = 0; i < reps; ++i) samples.push_back(measure<Book>(pmu, ops));

    std::printf("Hardware counter stats for './bench --impl %s --reps 1 --no-timers' (%d runs, median), %s\n\n",
                impl, reps, Book::name());
    auto med_ev = [&](size_t i) { std::vector<uint64_t> v; for (auto& s : samples) v.push_back(s.ev[i]); return median_of(v); };
    auto med_d = [&](double Sample::*f) { std::vector<double> v; for (auto& s : samples) v.push_back(s.*f); return median_of(v); };
    auto med_l = [&](long Sample::*f) { std::vector<long> v; for (auto& s : samples) v.push_back(s.*f); return median_of(v); };

    const uint64_t cycles = med_ev(0), instr = med_ev(1), branches = med_ev(2), br_miss = med_ev(3);
    std::printf("%16.2f msec task-clock\n", med_d(&Sample::task_ms));
    std::printf("%16.2f msec wall-clock\n", med_d(&Sample::wall_ms));
    std::printf("%16llu      cycles\n", (unsigned long long)cycles);
    std::printf("%16llu      instructions              # %6.2f  insn per cycle\n", (unsigned long long)instr,
                cycles ? double(instr) / cycles : 0.0);
    std::printf("%16llu      branches\n", (unsigned long long)branches);
    std::printf("%16llu      branch-misses             # %6.2f%% of all branches\n", (unsigned long long)br_miss,
                branches ? 100.0 * br_miss / branches : 0.0);
    for (size_t i = 4; i < kNumEvents; ++i) {
        std::printf("%16llu      %s\n", (unsigned long long)med_ev(i), kEvents[i].label);
    }
    std::printf("%16s      cache-misses (last level)   <not available on Apple Silicon PMU>\n", "n/a");
    std::printf("%16ld      page-faults (minor)\n", med_l(&Sample::minflt));
    std::printf("%16ld      page-faults (major)\n", med_l(&Sample::majflt));
    std::printf("\nChecksum: %llu\n", (unsigned long long)samples.front().checksum);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string impl;
    int reps = 5;
    size_t n_ops = 1'000'000;
    uint32_t seed = 42;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--impl" && i + 1 < argc) impl = argv[++i];
        else if (a == "--reps" && i + 1 < argc) reps = std::atoi(argv[++i]);
        else if (a == "--ops" && i + 1 < argc) n_ops = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--seed" && i + 1 < argc) seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else { std::fprintf(stderr, "usage: sudo %s --impl A|B|C [--reps R] [--ops N] [--seed S]\n", argv[0]); return 2; }
    }
    if (impl != "A" && impl != "B" && impl != "C") {
        std::fprintf(stderr, "usage: sudo %s --impl A|B|C [--reps R] [--ops N] [--seed S]\n", argv[0]);
        return 2;
    }

    // keep this thread on a P core so runs are comparable
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    Pmu pmu;
    if (!pmu.setup()) return 1;

    const std::vector<ob::Op> ops = ob::make_workload(n_ops, seed);
    if (impl == "A") return run<MapBook>(pmu, ops, reps, "A");
    if (impl == "B") return run<HeapBook>(pmu, ops, reps, "B");
    return run<ArrayBook>(pmu, ops, reps, "C");
}
