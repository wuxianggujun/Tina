# GCC 13 + GLFW/X11 + Xvfb (TEST-001 platform). China mirrors by default.
ARG BASE_IMAGE=docker.m.daocloud.io/library/ubuntu:24.04
FROM ${BASE_IMAGE}

ARG APT_MIRROR=mirrors.aliyun.com
ARG VCPKG_GIT_URL=https://mirrors.tuna.tsinghua.edu.cn/git/vcpkg.git
ARG VCPKG_BASELINE=81de6771512413aaf89ea77add5ad1fda126b9d0

ENV DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_FORCE_SYSTEM_BINARIES=1 \
    VCPKG_DISABLE_METRICS=1 \
    PATH=/opt/vcpkg:${PATH}

RUN set -eux; \
    if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then \
      sed -i \
        -e "s|http://archive.ubuntu.com/ubuntu|http://${APT_MIRROR}/ubuntu|g" \
        -e "s|https://archive.ubuntu.com/ubuntu|https://${APT_MIRROR}/ubuntu|g" \
        -e "s|http://security.ubuntu.com/ubuntu|http://${APT_MIRROR}/ubuntu|g" \
        -e "s|https://security.ubuntu.com/ubuntu|https://${APT_MIRROR}/ubuntu|g" \
        /etc/apt/sources.list.d/ubuntu.sources; \
    fi; \
    if [ -f /etc/apt/sources.list ]; then \
      sed -i \
        -e "s|http://archive.ubuntu.com/ubuntu|http://${APT_MIRROR}/ubuntu|g" \
        -e "s|http://security.ubuntu.com/ubuntu|http://${APT_MIRROR}/ubuntu|g" \
        /etc/apt/sources.list; \
    fi; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
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
        xauth; \
    rm -rf /var/lib/apt/lists/*; \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100; \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100; \
    update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-13 100; \
    update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-13 100

RUN set -eux; \
    if ! git clone --filter=blob:none "${VCPKG_GIT_URL}" "${VCPKG_ROOT}"; then \
      git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"; \
    fi; \
    git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_BASELINE}" \
      || git -C "${VCPKG_ROOT}" fetch --depth 1 https://github.com/microsoft/vcpkg.git "${VCPKG_BASELINE}"; \
    git -C "${VCPKG_ROOT}" checkout --force "${VCPKG_BASELINE}"; \
    "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics; \
    test -f "${VCPKG_ROOT}/versions/baseline.json"

WORKDIR /work/tina
CMD ["/bin/bash"]
