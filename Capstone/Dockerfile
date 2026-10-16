FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    linux-tools-common \
    linux-tools-generic \
    linux-tools-$(uname -r) || true \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN make clean && make

CMD ["/bin/bash", "./scripts/run_perf.sh"]
