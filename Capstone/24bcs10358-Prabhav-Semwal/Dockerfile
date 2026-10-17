FROM debian:12-slim
RUN apt-get update && apt-get install -y --no-install-recommends g++ make linux-perf time && rm -rf /var/lib/apt/lists/*
WORKDIR /work
COPY . .
RUN make -B
CMD ["./bench", "--verify"]
