# Clang 22 + GCC 15 libstdc++ + Ninja + vcpkg (TEST-001 Null/sanitize).
# China-friendly defaults: DaoCloud Ubuntu base, Aliyun apt, Tsinghua vcpkg + llvm-apt.
ARG BASE_IMAGE=docker.m.daocloud.io/library/ubuntu:24.04
FROM ${BASE_IMAGE}

ARG APT_MIRROR=mirrors.aliyun.com
ARG VCPKG_GIT_URL=https://mirrors.tuna.tsinghua.edu.cn/git/vcpkg.git
ARG VCPKG_BASELINE=81de6771512413aaf89ea77add5ad1fda126b9d0
ARG LLVM_APT_MIRROR=https://mirrors.tuna.tsinghua.edu.cn/llvm-apt

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
        ca-certificates \
        curl \
        gnupg \
        lsb-release \
        software-properties-common \
        wget; \
    # Official Launchpad PPA (add-apt-repository); Aliyun already accelerates archive.ubuntu.com.
    add-apt-repository -y ppa:ubuntu-toolchain-r/test; \
    # LLVM 22: prefer Tuna llvm-apt, fall back to apt.llvm.org
    wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
      | gpg --dearmor -o /usr/share/keyrings/llvm.gpg; \
    echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] ${LLVM_APT_MIRROR}/noble/ llvm-toolchain-noble-22 main" \
      > /etc/apt/sources.list.d/llvm22.list; \
    if ! apt-get update; then \
      echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/noble/ llvm-toolchain-noble-22 main" \
        > /etc/apt/sources.list.d/llvm22.list; \
      apt-get update; \
    fi; \
    apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        g++-15 \
        gcc-15 \
        clang-22 \
        clang++-22 \
        lld-22 \
        libclang-rt-22-dev \
        git \
        ninja-build \
        pkg-config \
        tar \
        unzip \
        zip \
        python3; \
    rm -rf /var/lib/apt/lists/*; \
    update-alternatives --install /usr/bin/clang clang /usr/bin/clang-22 100; \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-22 100; \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 90; \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-15 90; \
    clang-22 --version; \
    g++-15 --version

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
