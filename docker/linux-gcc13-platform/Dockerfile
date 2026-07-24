# GCC 13 + GLFW/X11 deps + Xvfb for headless platform tests (TEST-001 tail).
# Build context: repo root. Reuses same vcpkg baseline as Null image.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_FORCE_SYSTEM_BINARIES=1 \
    VCPKG_DISABLE_METRICS=1 \
    PATH=/opt/vcpkg:${PATH}

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        g++-13 \
        gcc-13 \
        git \
        ninja-build \
        pkg-config \
        tar \
        unzip \
        zip \
        python3 \
        libx11-dev \
        libxrandr-dev \
        libxinerama-dev \
        libxcursor-dev \
        libxi-dev \
        libgl1-mesa-dev \
        libxext-dev \
        xvfb \
        xauth \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-13 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-13 100

ARG VCPKG_BASELINE=81de6771512413aaf89ea77add5ad1fda126b9d0
RUN git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    && git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_BASELINE}" \
    && git -C "${VCPKG_ROOT}" checkout --force "${VCPKG_BASELINE}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics \
    && test -f "${VCPKG_ROOT}/versions/baseline.json"

WORKDIR /work/tina
CMD ["/bin/bash"]
