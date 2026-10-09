# SPSC Queue with std::mutex

Name: Apurv Dugar
Roll No: 24BCS10107
Email: apurv.24bcs10107@sst.scaler.com

## Benchmark Results

Workload: **10,000,000 push and pop operations** of 64-byte objects between two threads (`producerThread` and `consumerThread`).

| Build Configuration | Optimization Level | Time Taken | Throughput (objects/sec) | Data Throughput |
| :--- | :--- | :--- | :--- | :--- |
| `g++ -O0` | No Optimization | ~3.24 s | **~3.09 × 10⁶ objects/sec** | ~198 MB/s |
| `g++ -O3` | Full Optimization | ~0.76 s | **~13.23 × 10⁶ objects/sec** | ~847 MB/s |

### Key Numbers (with `-O3`):
- **Time Taken**: `0.755807 seconds`
- **Throughput**: `1.32309e+07 objects per second` (~13.23 million 64-byte objects / sec)

---

![Benchmark Output](image.png)
