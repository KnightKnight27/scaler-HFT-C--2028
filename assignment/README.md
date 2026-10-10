# SPSC Queue Benchmark Using `std::mutex`

**Name:** Ansh Mahajan\
**Roll No.:** 24BCS10345\
**Email:** ansh.24bcs10345@sst.scaler.com

## Overview

This project implements a single-producer, single-consumer (SPSC) queue
using `std::mutex`. The benchmark measures how many 64-byte objects the
producer can push and the consumer can pop during an approximately
one-second run.



## How the benchmark works

1.  Create the queue and start the producer and consumer threads.
2.  Start the elapsed-time measurement.
3.  Let the threads run while the main thread sleeps for one second.
4.  Set `stop = true`.
5.  Join both threads.
6.  Print elapsed time, total successful pushes/pops, and throughput.

Throughput is calculated as:

``` cpp
push throughput = pushed / elapsed_seconds;
pop throughput  = popped / elapsed_seconds;
```

The timing and counts are approximate. In the current program, the
worker threads can begin processing before the timer starts, and the
elapsed interval includes thread shutdown/joining. Therefore, the output
is useful as an initial benchmark, but it is not a precise count of
operations performed strictly inside the measured interval.

## Build and run

The commands below were used from Windows Command Prompt in the
`assignment` directory.

Optimized build:

``` cmd
g++ -O0 -std=c++14 -pthread spsc_queue.cpp -o benchmark.exe
benchmark.exe
```

Additional runs were made with `-O2` and different C++ language-standard
flags. Results from different optimization settings should not be
treated as a direct apples-to-apples comparison.

## Benchmark results

The values below are copied from the program output. Throughput is shown
in millions of objects per second (M objects/s).

  ------------------------------------------------------------------------------------------
         Run Build flags           Elapsed      Pushed      Popped         Push          Pop
                                       (s)                           throughput   throughput
                                                                          (M/s)        (M/s)
  ---------- ------------------ ---------- ----------- ----------- ------------ ------------
           1 `-O2 -std=c++20`       1.0038   4,949,402   4,948,379      4.93067      4.92965

           2 `-O0 -std=c++23`       1.0051   3,846,009   3,845,573      3.82650      3.82607

           3 `-O0 -std=c++23`      1.00051   3,758,136   3,757,118      3.75622      3.75520

           4 `-O0 -std=c++23`      1.00758   3,354,271   3,353,352      3.32905      3.32814

           5 `-O0 -std=c++14`      1.00083   3,777,409   3,776,993      3.77427      3.77385

           6 `-O0 -std=c++14`      1.00444   3,214,044   3,214,043      3.19983      3.19983

           7 `-O0 -std=c++14`      1.01396   3,543,983   3,542,960      3.49519      3.49418

           8 `-O0 -std=c++20`      1.00607   3,962,872   3,961,907      3.93897      3.93801

           9 `-O0 -std=c++20`      1.00793   3,615,476   3,614,551      3.58704      3.58612

          10 `-O0 -std=c++20`      1.00757   3,521,730   3,521,095      3.49526      3.49463
  ------------------------------------------------------------------------------------------

### Notes on the results

-   The optimized `-O2 -std=c++20` run reached approximately **4.93
    million pushes/s** and **4.93 million pops/s**.
-   The nine `-O0` runs varied from approximately **3.20 to 3.94 million
    objects/s**. These runs used different language-standard flags
    (`c++14`, `c++20`, and `c++23`), so they should be considered
    individual observations rather than a controlled comparison between
    standards.
-   Push and pop totals are close, but they do not have to be identical
    when the stop flag is set. Objects may remain in the queue when the
    threads stop.



## Conclusion

The current mutex-based SPSC queue achieved approximately **4.93 million
pushes and 4.93 million pops per second** in the recorded optimized run.
The unoptimized runs were lower and varied between trials. 
