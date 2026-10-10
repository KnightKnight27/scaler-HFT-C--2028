# SPSC Queue (Single Producer Single Consumer)

## Author Information
* **Name:** Abhiroop Sistu
* **Email:** abhiroop.24bcs10287@sst.scaler.com
* **Roll Number:** 24bcs10287

---

## Overview
This program implements a **Single Producer Single Consumer (SPSC) queue** using `std::mutex`. 

* **Producer:** Pushes 64-byte objects into the queue.
* **Consumer:** Pops 64-byte objects from the queue.
* **Buffer Size:** Fixed-size buffer of 1024 objects.
* **Concurrency:** Both threads run concurrently and are safely joined using `join()`.

---

## How to Run

1. **Compile the program:**
   ```bash
   g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
   ```

2. **Run the executable:**
   ```bash
   ./spsc_queue
   ```

---

## Benchmark Results

The program was tested by transferring **5,000,000 objects**, each of size **64 bytes**, across three separate runs:

| Run | Run Time (seconds) | Objects per Second |
|:---:|:---:|:---:|
| 1 | 0.513093 | 9,744,810 |
| 2 | 0.538922 | 9,277,790 |
| 3 | 0.624972 | 8,000,360 |

* **Overall Throughput:** Averaged approximately **8.95 million objects per second** (note that average throughput can vary depending on CPU load and system conditions).
* **Combined Push/Pop Operations:** Counting both operations for each object, the measured operation rates were approximately **19.49 million**, **18.56 million**, and **16.00 million operations per second** across the three runs.

---

## Correctness Check

* **Data Integrity:** Each run successfully transferred all 5,000,000 objects and produced the expected checksum: 
  ```text
  12499997500000
  ```
* **Order Verification:** The consumer actively verifies that all objects are received in the exact expected sequential order.