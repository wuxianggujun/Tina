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

# SDK-001 跨发行版 consumer（Debian 13 + GCC 14 + 独立 vcpkg 树）
# 只用于跨发行版 artifact transfer 门禁；不拷贝 Tina 源码进镜像。
docker build -f docker/linux-sdk-consumer-debian13/Dockerfile -t tina-sdk-consumer-debian13:sdk-001 .
```

以上 4 个即 `docker/` 下的全部 Dockerfile。前三个由 `tools/windows/RunLinuxDockerGate.ps1` 使用，
第四个由 `tools/windows/RunSdkCrossDistroGate.ps1` 使用（两个脚本都会在需要时自行 build，
通常不必手工执行上面的命令）。

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

见 `tools/windows/RunLinuxDockerGate.ps1` 与 `docs/evidence/m12-evidence-linux.md`。

## 回收

**必须定向清理，不得用全局 prune 代替**（见 [building.md](../docs/building.md) 的临时资源生命周期一节：
只清理本轮账簿中由本轮创建或明确独占的资源，不猜测、不全局 prune）。

正常情况下无需手工回收：`RunLinuxDockerGate.ps1` 以 `--rm` 启动容器，并在启动前按名字
`docker rm -f <container>` 清掉残留；gate 退出码为 0 时还会断言容器已消失。只有异常中断后才需要手工补做，
按**名字**而非全局清理：

```powershell
# 1) 按名字删除本轮容器（只列你实际跑过的那个）
docker rm -f tina-test-001-gcc13-null
docker rm -f tina-test-001-gcc13-platform
docker rm -f tina-test-001-clang22-null
docker rm -f tina-test-001-clang22-sanitize
docker rm -f tina-sdk-001-gcc13-consumer
docker rm -f tina-2d-editor-gcc13-zenity
docker rm -f tina-2d-editor-gcc13-kdialog

# 2) 确认没有残留（应无输出）
docker container ls -a --filter 'name=^/tina-' --format '{{.Names}}'

# 3) 仅在确认不再需要时按 tag 删除镜像
docker rmi tina-linux-gcc13:test-001 tina-linux-gcc13-platform:test-001 `
  tina-linux-clang22:test-001 tina-sdk-consumer-debian13:sdk-001
```

容器名以 `-Gate` 取值为准，权威清单在 `tools/windows/RunLinuxDockerGate.ps1` 的 `$map`。
`RunLinuxDockerGate.ps1 -PruneAfter` 内部确实会调用 `docker container prune` / `image prune`，
那是**显式 opt-in**；不要把它当默认收尾手段，也不要用 `docker system prune`。
容器内 build tree 位于挂载出来的 `out/build/docker-*`，对 Windows 侧可见，需按路径单独删除。
