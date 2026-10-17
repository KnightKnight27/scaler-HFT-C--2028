# we're on windows, so perf runs inside this container (docker desktop uses the WSL2 kernel)
# docker build -t ob-perf -f perf.Dockerfile .
# docker run --rm --privileged -v "$(pwd):/work" ob-perf ./run_perf.sh
# (from git bash on windows we needed MSYS_NO_PATHCONV=1 and "$(pwd -W):/work")
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends g++ make linux-tools-generic linux-tools-common && rm -rf /var/lib/apt/lists/*
RUN ln -sf $(ls -d /usr/lib/linux-tools/*/ | head -1)perf /usr/local/bin/perf
WORKDIR /work
