# Physics3D / Floating Origin 交接

## 范围与隔离

- 工作区：`C:/Users/wuxianggujun/.codex/worktrees/c49a/Tina`。未提交、未合并，未访问其他 worktree 的 build tree 或 D 盘 SDK 安装目录。
- 新增实现仅在 `include/tina/physics3d`、`src/physics3d`、`tests/physics3d`；现有 AI、NavigationAgent、Water 与 AssetGpuUpload 修改均保留。
- 公共接线是精确增量：root CMake、Dependencies、preset、vcpkg feature、SDK export/install、header token gate、tests 子目录；`ErrorDomain::Physics3D = 23`，保留现有 `AI = 22`。
- 已向另外两个 Tina 任务通知范围。合并时按 Physics3D hunk 合入，不覆盖同文件里的 AI/Water/Runtime 工作。
- 接口与限制见 [Physics3D](physics3d.md)，新增版本/线程/坐标决策记录见 Proposed [ADR 0050](adr/0050-jolt-physics3d-floating-origin.md)。

## 实现

可选 `Tina::Physics3D` 链接 Jolt 5.5.0 PRIVATE；one-owner fixed-step world、Box/Sphere/Capsule body、
generation/owner 校验、直接变换/速度/impulse/awake 操作、closest ray 与 bounded broadphase AABB query。

Floating origin 使用 double global + float local；`shiftOrigin()` 在完整预检查后平移所有 body，显式更新
broadphase/contact cache，最后只发布一次 origin revision。保持 body ID、rotation、velocity 和 awake 状态；
active body 的 sleep-test timer 重启是明确的 Jolt API 限制。零位移不发布，非法/越界位移不做半份 mutation。
没有旧 API、旧 wire schema 或兼容分支需要保留，也没有修改现有 Scene/Navigation/Render 的坐标模型。

## 编译证据

平台：Windows，MSVC 19.50.35728.0，Visual Studio 18，CMake 4.2.3-msvc3；Jolt 5.5.0、GoogleTest 1.17.0。

1. 首次 configure exit 1：vcpkg 的 Jolt port 不提供 ConfigVersion，`find_package(Jolt 5.5.0 EXACT)` 被拒绝。
   已改为 `find_package(Jolt CONFIG)`，实际版本通过适配层 compile-time 断言及 Create 时的 Jolt feature ABI 检查约束。
2. 同一临时树再次 configure exit 0，依赖已经安装，不重复构建 Jolt。
3. 最小 target `tina_physics3d_tests` 的首次及最后一次增量 build 均 exit 0；最后一次产物时间为
   `2026-09-06T17:21:36+08:00`。闭包只有 Core、Physics3D、4 个 header-isolation TU 与 15 个 unit 用例源码，未构建 bgfx/shaderc/Editor/产品 sample。
4. `git diff --check` exit 0；新增模块文件 strict UTF-8 检查通过，公开头无 Jolt/native/其他 backend token；vcpkg/preset JSON 解析通过。

`testRuns=0`、`sampleRuns=0`。没有运行 GoogleTest、CTest、smoke、视觉或性能 gate，不应把这里的 Build 证据写成测试通过。

保留的可执行程序与运行所需 DLL：

- `artifacts/physics3d-20260906/tina_physics3d_tests.exe`，5,354,496 bytes。
- `gtest.dll`、`gtest_main.dll` 与程序同目录；Jolt 静态链接。
- SHA256：`7163498F58B2ECCD0FB7FA6B29E8DDB4A2FA69E01E9B265E3ED9A134082AA18E`。
- 同目录保留 `configure-first-failure.log`、`configure.log`、`build.log`；交付目录合计 7,449,633 bytes。
- 新模块 13 个源码/CMake 文件的清单指纹：`B01CF73795ADBA58849AECC686321243B035E545CF36D9657AAE38F64B85A204`。
  算法：按完整文件路径排序，每条为 `repo-relative/path:UPPERCASE_SHA256`，用 LF 连接且无末尾 LF，再对 UTF-8 字节取 SHA256。

后续得到测试授权后，直接使用已编译产物即可，不必先重复 configure/build：

```powershell
artifacts/physics3d-20260906/tina_physics3d_tests.exe `
  --gtest_filter=PhysicsWorld3DTests.*:FloatingOrigin3DTests.*
```

## 资源账本

计数范围仅为本任务，不把其他任务或全机资源算成本轮资源。

| 字段 | 核验结果 |
| --- | --- |
| buildTree | `out/build/windows-msvc-vnext-physics3d`，回收前 299,241,863 bytes；执行策略拒绝删除命令，复查仍存在，回收后仍为 299,241,863 bytes。**未回收**，不是常驻核心树 |
| process | 本轮 configure/build jobs 均已终止；按此 build path / `tina_physics3d` 过滤 CMake/MSBuild/cl/link/ninja/vcpkg/mspdbsrv 的实际查询为 0 |
| helper/watchdog | 本轮未创建额外 helper/watchdog，0 |
| container/volume/image | 本轮未创建或使用容器、volume、image，均为 0；未清理其他任务资源 |
| cache | `D:/Programs/vcpkg` 的共享 Jolt source/package/download/binary cache 保留，不属于临时 worktree 清理范围；未声称共享 cache 已释放 |
| agent | 本轮未创建子 agent，0；仅发送了范围协调消息 |
| artifacts | 交付的 exe/DLL/log 合计 7,449,633 bytes，有意保留；不属于源码提交内容 |

删除命令被拒绝发生在执行前，未绕过策略换其他 shell 删除；因此不能声称“资源全部释放”。

## 下一步

1. 明确授权后执行上述 15 个 unit，用真实求解/查询确认 rebase，不用编译结果替代。
2. 合并时保留其他任务的 shared CMake/SDK/ErrorDomain 增量；此 worktree 没有替主工作区编译或安装。
3. 单独冻结 Scene3D bridge：physics state 为权威，Scene root/camera/light/interpolation/cache 同一安全帧换坐标，避免父子重复平移；不要默默修改当前 NavigationVolume3D 的坐标。
4. 后续再做真实产品消费者、installed consumer、跨平台和性能预算。Joints、compound/mesh、CCD、contact event、character controller 与 Editor authoring 尚未提供。
