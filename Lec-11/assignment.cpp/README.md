Name: Sushant Bajaj
Email: sushant24bcs10262@sst.scaler.com

spsc queue using mutex

Build:

`g++ -O0 Lec-11/assignment.cpp/spsc_queue.cpp -o a `
or
`g++ -O2 Lec-11/assignment.cpp/spsc_queue.cpp -o a `

Run:
`./a`

Run results with O0-

`
pushes/sec: 3682385
pops/sec:   3681405
`


Run with O2 optimizations-

`pushes/sec: 10036555
pops/sec:   10036529`