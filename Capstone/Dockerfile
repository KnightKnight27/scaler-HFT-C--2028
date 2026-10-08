FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    linux-tools-common \
    linux-tools-generic \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN make clean && make -j$(nproc)

CMD ["bash", "scripts/run_perf.sh"]
