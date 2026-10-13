# Correctness Results

Command:

```text
./build/bench --verify
```

Result:

```text
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```

The verification stream contains 1,000,000 operations generated with seed `42`. BBO, bid depth, ask depth, prices, quantities, and cancellation behavior are compared after each operation.
