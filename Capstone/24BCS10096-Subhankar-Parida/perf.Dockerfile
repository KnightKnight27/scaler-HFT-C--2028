FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y g++ linux-perf valgrind && rm -rf /var/lib/apt/lists/*
