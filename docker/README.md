# Tina Docker 镜像（TEST-001 Linux）

默认使用**国内加速源**，降低从 Docker Hub / Ubuntu / GitHub 拉取的时延。

## 默认镜像源

| 用途 | 默认 |
| --- | --- |
| 基础镜像 | `docker.m.daocloud.io/library/ubuntu:24.04` |
| Ubuntu apt | `mirrors.aliyun.com` |
| vcpkg git | `mirrors.tuna.tsinghua.edu.cn/git/vcpkg.git` |
| LLVM apt（Clang 22） | `mirrors.tuna.tsinghua.edu.cn/llvm-apt` |
| GCC 15 PPA 近似 | `mirrors.tuna.tsinghua.edu.cn/ubuntu-toolchain-r` |

## 构建示例

```powershell
# GCC13 Null
docker build -f docker/linux-gcc13/Dockerfile -t tina-linux-gcc13:test-001 .

# GCC13 Platform
docker build -f docker/linux-gcc13-platform/Dockerfile -t tina-linux-gcc13-platform:test-001 .

# Clang22（需能访问 LLVM/PPA 镜像）
docker build -f docker/linux-clang22/Dockerfile -t tina-linux-clang22:test-001 .
```

### 覆盖镜像参数

```powershell
docker build -f docker/linux-gcc13/Dockerfile `
  --build-arg BASE_IMAGE=docker.m.daocloud.io/library/ubuntu:24.04 `
  --build-arg APT_MIRROR=mirrors.tuna.tsinghua.edu.cn `
  --build-arg VCPKG_GIT_URL=https://mirrors.tuna.tsinghua.edu.cn/git/vcpkg.git `
  -t tina-linux-gcc13:test-001 .
```

海外网络可改回官方源：

```powershell
docker build -f docker/linux-gcc13/Dockerfile `
  --build-arg BASE_IMAGE=ubuntu:24.04 `
  --build-arg APT_MIRROR=archive.ubuntu.com `
  --build-arg VCPKG_GIT_URL=https://github.com/microsoft/vcpkg.git `
  -t tina-linux-gcc13:test-001 .
```

## Docker Desktop 注册表加速（可选）

Docker Engine → Docker Engine JSON，例如：

```json
{
  "registry-mirrors": [
    "https://docker.m.daocloud.io"
  ]
}
```

Apply & Restart 后 `docker pull` 会更快。

## 运行门禁

见 `tools/windows/RunLinuxDockerGate.ps1` 与 `docs/m12-evidence-linux.md`。

## 回收

```powershell
docker container prune -f
docker image prune -f
# 仅在确认不再需要时删除镜像：
# docker rmi tina-linux-gcc13:test-001 tina-linux-gcc13-platform:test-001
```
