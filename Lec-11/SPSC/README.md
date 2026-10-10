#SPSC Queue with Mutex and Spinlock

Name: Harshit Sen
email: harshit.24bcs10003@sst.scaler.com
roll_no: 10003 

For this assignment the class SPSC is same across both the files.
## Build and Run

For compiling the files(for Ubuntu).

```bash
g++ -O0 -g -std=c++17 -pthread spinlock.cpp -o spinlock
g++ -O0 -g -std=c++17 -pthread mutex.cpp -o mutex
```

For running the performance test ->

```bash
perf stat ./spinlock
perf stat ./mutex
```
##The result
### Spinlock `perf stat` Results

**Throughput**

| Operation | Ops / Sec |
| :--- | :--- |
| **Objects Pushed** | 4,367,835 |
| **Objects Popped** | 4,367,835 |

**Hardware & OS Metrics**

| Metric | Count / Value | Rate / Note |
| :--- | :--- | :--- |
| **Context Switches** | 0 | 0.0 cs/sec |
| **CPU Migrations** | 0 | 0.0 migrations/sec |
| **Page Faults** | 163 | 81.3 faults/sec |
| **Task Clock** | 2004.26 msec | 1.9 CPUs utilized |
| **Branch Misses** | 8,934,680 | 4.0% miss rate |
| **Branches** | 230,454,020 | 115.0 M/sec |
| **CPU Cycles** | 5,536,866,677 | 2.8 GHz |
| **Instructions** | 2,090,849,188 | **0.4 IPC** (Instructions per cycle) |

**Execution Time**

| Time Type | Duration (Seconds) | Description |
| :--- | :--- | :--- |
| **Time Elapsed** | 1.006685910 s | Total wall-clock time the benchmark ran. |
| **User Time** | 1.993074000 s | Total CPU time spent in user-space (across ~2 cores). |
| **Sys Time** | 0.011958000 s | Total CPU time spent in the OS kernel. |


### Mutex `perf stat` Results

**Throughput**

| Operation | Ops / Sec |
| :--- | :--- |
| **Objects Pushed** | 2,802,631 |
| **Objects Popped** | 2,802,631 |

**Hardware & OS Metrics**

| Metric | Count / Value | Rate / Note |
| :--- | :--- | :--- |
| **Context Switches** | 0 | 0.0 cs/sec |
| **CPU Migrations** | 0 | 0.0 migrations/sec |
| **Page Faults** | 161 | 82.2 faults/sec |
| **Task Clock** | 1958.98 msec | 1.9 CPUs utilized |
| **Branch Misses** | 3,218,110 | 1.0% miss rate |
| **Branches** | 310,274,857 | 158.4 M/sec |
| **CPU Cycles** | 2,331,913,825 | 1.2 GHz |
| **Instructions** | 1,725,673,930 | **0.7 IPC** (Instructions per cycle) |

**Execution Time**

| Time Type | Duration (Seconds) | Description |
| :--- | :--- | :--- |
| **Time Elapsed** | 1.021477634 s | Total wall-clock time the benchmark ran. |
| **User Time** | 1.227878000 s | Total CPU time spent in user-space. |
| **Sys Time** | 0.732634000 s | Total CPU time spent in the OS kernel. |
