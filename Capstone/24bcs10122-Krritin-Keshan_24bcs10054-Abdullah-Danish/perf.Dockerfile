FROM debian:bookworm
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends g++ make linux-perf valgrind && rm -rf /var/lib/apt/lists/*
