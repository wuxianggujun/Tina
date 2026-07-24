# Clang 22 + GCC 15 libstdc++ + Ninja + vcpkg for TEST-001 Null / sanitizer graphs.
# Build context: repo root. Do not inherit a broken host HTTP_PROXY (e.g. 127.0.0.1:7890).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_FORCE_SYSTEM_BINARIES=1 \
    VCPKG_DISABLE_METRICS=1 \
    PATH=/opt/vcpkg:${PATH} \
    HTTP_PROXY= \
    HTTPS_PROXY= \
    http_proxy= \
    https_proxy= \
    ALL_PROXY= \
    all_proxy=

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        gnupg \
        lsb-release \
        software-properties-common \
        wget \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/noble/ llvm-toolchain-noble-22 main" \
        > /etc/apt/sources.list.d/llvm22.list \
    && apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        g++-15 \
        gcc-15 \
        clang-22 \
        clang++-22 \
        lld-22 \
        git \
        ninja-build \
        pkg-config \
        tar \
        unzip \
        zip \
        python3 \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-22 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-22 100 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 90 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-15 90 \
    && clang-22 --version \
    && g++-15 --version

ARG VCPKG_BASELINE=81de6771512413aaf89ea77add5ad1fda126b9d0
RUN git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    && git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_BASELINE}" \
    && git -C "${VCPKG_ROOT}" checkout --force "${VCPKG_BASELINE}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics \
    && test -f "${VCPKG_ROOT}/versions/baseline.json"

WORKDIR /work/tina
CMD ["/bin/bash"]
