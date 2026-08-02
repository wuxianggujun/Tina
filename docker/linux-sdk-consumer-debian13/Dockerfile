# SDK-001 cross-distribution consumer: Debian 13 + GCC 14 + an independent vcpkg tree.
# Build context: repository root. The Tina producer source is not copied into this image.
ARG BASE_IMAGE=docker.m.daocloud.io/library/debian:13-slim
FROM ${BASE_IMAGE}

ARG APT_MIRROR=mirrors.aliyun.com
ARG VCPKG_GIT_URL=https://mirrors.tuna.tsinghua.edu.cn/git/vcpkg.git
ARG VCPKG_BASELINE=81de6771512413aaf89ea77add5ad1fda126b9d0

ENV DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_FORCE_SYSTEM_BINARIES=1 \
    VCPKG_DISABLE_METRICS=1 \
    VCPKG_DEFAULT_TRIPLET=x64-linux \
    VCPKG_MAX_CONCURRENCY=2 \
    PATH=/opt/vcpkg:${PATH}

RUN set -eux; \
    if [ -f /etc/apt/sources.list.d/debian.sources ]; then \
      sed -i \
        -e "s|http://deb.debian.org/debian|http://${APT_MIRROR}/debian|g" \
        -e "s|https://deb.debian.org/debian|https://${APT_MIRROR}/debian|g" \
        -e "s|http://security.debian.org/debian-security|http://${APT_MIRROR}/debian-security|g" \
        -e "s|https://security.debian.org/debian-security|https://${APT_MIRROR}/debian-security|g" \
        /etc/apt/sources.list.d/debian.sources; \
    fi; \
    if [ -f /etc/apt/sources.list ]; then \
      sed -i \
        -e "s|http://deb.debian.org/debian|http://${APT_MIRROR}/debian|g" \
        -e "s|http://security.debian.org/debian-security|http://${APT_MIRROR}/debian-security|g" \
        /etc/apt/sources.list; \
    fi; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        g++-14 \
        gcc-14 \
        git \
        ninja-build \
        pkg-config \
        python3 \
        tar \
        unzip \
        util-linux \
        zip; \
    rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    if ! git clone --filter=blob:none "${VCPKG_GIT_URL}" "${VCPKG_ROOT}"; then \
      git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"; \
    fi; \
    git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_BASELINE}" \
      || git -C "${VCPKG_ROOT}" fetch --depth 1 https://github.com/microsoft/vcpkg.git "${VCPKG_BASELINE}"; \
    git -C "${VCPKG_ROOT}" checkout --force "${VCPKG_BASELINE}"; \
    "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics; \
    test -f "${VCPKG_ROOT}/versions/baseline.json"; \
    "${VCPKG_ROOT}/vcpkg" install xxhash:x64-linux --clean-after-build

COPY tests/sdk_consumer/ /opt/tina-sdk-consumer/
COPY cmake/VerifyInstalledTinaSdkHeaders.cmake \
     cmake/VerifyRelocatedTinaSdkPackage.cmake \
     /opt/tina-sdk-gate/cmake/
COPY tools/linux/run-sdk-cross-distro-consumer-gate.sh \
     /usr/local/bin/tina-sdk-cross-distro-consumer-gate

RUN chmod 0755 /usr/local/bin/tina-sdk-cross-distro-consumer-gate

WORKDIR /work/cross-distro-gate
CMD ["/usr/local/bin/tina-sdk-cross-distro-consumer-gate"]
