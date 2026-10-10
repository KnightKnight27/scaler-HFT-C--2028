mail: naverdo.24bcs10076@sst.scaler.com

roll_no: 10076

Used a ring buffer SPSC queue with mutexes of various sizes and got results as in the image
![benchmark](image.png)

Best time was with O2 optimisation and 1000 sized queue. No significant performance difference between std and -pthread on my system (Ubuntu 24 in WSL2, 14900hx).

