# ADR 0024：SDK 版本、ABI 与兼容性策略

- 状态：Proposed
- 日期：2026-08-02
- 决策者：Tina maintainers

## 背景

Tina 当前版本是 `0.0.1`，以静态 C++23 库和版本化 CMake package 发布。`Tina::GameSDK` 以及可选
adapter component 已有 Windows/Linux installed consumer、component isolation、依赖闭包和 moved-prefix
relocatability 证据；跨发行版 artifact transfer gate 只会增加“一个 producer artifact 可由另一个环境中的
consumer 重新编译、链接并运行”的证据。

这些证据不能单独证明正式 ABI 兼容：

- 公共头使用 STL、PMR、模板、inline 函数和 Tina-owned C++ 类型，调用方与静态库会共同受 compiler、标准库、
  C/C++ runtime、编译配置和 feature graph 影响；
- moved-prefix 证明 package 不依赖原 source/build/install 绝对路径，不证明旧对象文件能链接新 archive；
- 跨发行版 consumer 若用候选 SDK 头重新编译，只证明该实际组合可消费 artifact，不证明任意发行版、compiler
  或 STL 组合兼容；
- `TinaConfigVersion.cmake` 当前由 CMake `SameMajorVersion` 生成。它只是版本选择机制；在 major 仍为 `0` 时，
  不能据此推导所有 `0.x` 版本兼容，也没有 ADR、compatibility matrix 或 baseline 支撑这种承诺。

因此“可安装、可搬移、可在另一环境重新编译”与“已发布、版本可协商、旧二进制兼容”必须分开记录。

## 术语

本 ADR 分别验证四种能力，不用一个“ABI 兼容”结论代替：

| 能力 | 证明内容 | 不证明的内容 |
| --- | --- | --- |
| Package relocatability | 安装前缀移动后仍可 `find_package`、链接和运行；package 不泄漏 producer 路径 | 旧对象文件与新 archive 兼容 |
| Source compatibility | 既有 consumer 源码可用候选 SDK 头重新编译 | 已编译对象无须重编译 |
| Binary/link compatibility | 既有 consumer 对象可与候选 archive 重新链接并通过运行 probe | 任意 toolchain/配置或行为完全相同 |
| Runtime/data compatibility | 版本化 cooked data、协议、持久化状态和可观察行为满足各自契约 | C++ link ABI 自动兼容 |

“支持的 ABI”只表示某个明确 compatibility tuple 内、某个已发布版本范围中的承诺，不表示 Tina 存在跨所有
Windows/Linux、发行版和 C++ toolchain 的单一 ABI。

## 建议决定（待接受）

### 1. 版本规则

推荐采用以下 pre-1.0 SemVer 约束：

- `0.y.z` 中 `y` 是 pre-1.0 compatibility epoch；有意破坏已发布 Public API、link ABI 或已承诺行为时递增
  `y`，并把 `z` 归零；当前 `0.0.1` 的下一次此类破坏至少发布为 `0.1.0`；
- `z` 只用于同一 compatibility tuple 内保持 source/link 兼容的修复和加法；不能用 patch release 删除公共
  target/header/symbol、改变公开类型 layout、重编号已发布枚举值，或提高既有 component 的必需依赖；
- `1.0.0` 起按常规 SemVer：major 允许 breaking change，minor 只增加兼容能力，patch 只做兼容修复；
- cooked schema、shader payload、benchmark schema 等已有独立版本的格式继续独立演进。它们的兼容性不能由
  SDK SemVer 代替，SDK release 只记录其支持矩阵。

在首个受支持 baseline 和 previous-release probe 落地前，推荐 CMake package 对 pre-1.0 consumer 采用精确版本
选择。baseline 覆盖完整 compatibility epoch 后，才可改为自定义 version check 或等价的 same-minor 选择。
当前 `SameMajorVersion` 保留为待实施修正的事实，在本 Proposed ADR 中不修改 CMake，也不构成发布承诺。

维护者接受本 ADR 时需要在以下选项中明确选择：

1. **推荐：minor epoch + compatible patch。** 配合 baseline/probe 后允许 `0.y.z` patch 自动协商；
2. **更保守：pre-1.0 永远精确版本。** 每个版本都要求 consumer 明确更新，仍执行 baseline 以识别意外破坏；
3. **不建议：沿用 `SameMajorVersion` 作为兼容规则。** major 为 `0` 时范围过宽，无法表达 pre-1.0 epoch。

### 2. Compatibility tuple

二进制兼容性只在发布清单列出的完整 tuple 内评估：

| 维度 | 至少记录的值 |
| --- | --- |
| OS ABI | Windows SDK/最低系统，或 Linux kernel userspace ABI + 最低 glibc 基线 |
| Architecture | `x86_64` 等 architecture、指针宽度和 endianness |
| Compiler | compiler family、major version或经验证的 version range |
| C++ standard library | MSVC STL/libstdc++/libc++、版本及 `_GLIBCXX_USE_CXX11_ABI` 等 ABI mode |
| Runtime linkage | MSVC `/MD`/`/MDd` 等 CRT 模式，或 Linux C/C++ runtime 组合 |
| Build configuration | Debug/Release、iterator/debug mode、sanitizer、LTO 及其他 ABI-relevant option |
| SDK graph | Tina version、requested component、公开 compile definition 和公共 dependency version |

Release 与 Debug 不交叉承诺；sanitized 与非 sanitized artifact 不交叉承诺。Windows/MSVC 与 Linux/GCC/Clang
分别建立 matrix，不能用一个平台的结果外推另一个平台。compiler major 不同、GCC 与 Clang 共用 libstdc++等
组合只有在 tuple 被明确列出并通过 probe 后才受支持。

CMake version selection 只能判断 Tina 版本，不能认证完整 tuple。可在 configure 阶段可靠检测的 tuple mismatch
应 fail closed；无法可靠检测的值必须进入 artifact metadata、matrix 和 consumer 文档，不得默认为兼容。

### 3. Baseline 与 compatibility probe

每个准备发布的受支持 tuple 至少保存以下版本化证据：

1. **Release manifest：** Tina 版本/commit、tuple、component/target、公共 compile requirement、依赖版本、
   archive/header 哈希和构建命令；
2. **Public API baseline：** 安装 headers、CMake imported target/component、公开宏与 compile definition 的清单，
   并用 representative consumer source 覆盖 GameSDK 和每个发布 adapter；
3. **Symbol/ABI baseline：** 每个静态 archive 的定义/未定义 symbol 清单和适用平台的 ABI report。Linux 优先
   评估 `libabigail`/`abidiff`；Windows 保存稳定化的 linker member/symbol report；工具噪声必须受审，不能
   仅以 symbol 数量相等判定兼容；
4. **Previous-release probes：** 既有 consumer 源码用候选 SDK 重编译，且由上一发布 SDK 头编译保留的对象
   与候选 archive 重新链接、运行。公共模板、inline、layout、exception/allocator ownership 需要专门 fixture，
   因为 symbol diff 无法覆盖它们；
5. **Package probes：** moved-prefix、component isolation、依赖闭包和 fresh consumer artifact transfer。它们是
   package/source portability gate，继续与 previous-object binary probe 分栏报告；
6. **Runtime/data probes：** 只对已声明稳定的 cooked schema、协议和行为运行跨版本 fixture，不把普通 sample
   smoke 写成数据兼容保证。

baseline 必须来自已发布 artifact 或不可变 release candidate，不得从同一个候选构建同时生成“旧”“新”两端。
工具不能可靠分析的公共 C++ surface 由 fixture 和人工 review 补足，并在报告中保留未覆盖项。

### 4. 公共变更流程

修改 `include/tina`、安装 target/component、公共 compile definition/dependency 或可观察的跨模块契约时：

1. 先分类为 internal、compatible additive、deprecated 或 breaking，并列出受影响 tuple/component；
2. compatible additive 仍运行 current consumer、baseline diff 和 previous-release source/object probes；
3. deprecation 使用标准属性或文档化 CMake 迁移诊断，必须给 replacement 和计划 removal epoch。pre-1.0 正常
   情况至少跨一个已发布 `0.y` epoch 后再删除；1.x 起至少跨一个已发布 minor，并只在下一 major 删除；
4. compatibility alias 只在确有已发布 consumer 时增加，必须有测试、明确 owner 和 removal version；不能借 alias
   恢复 Legacy surface、暴露 backend 类型或长期维护两套状态源；
5. breaking change 必须在发布前新增/更新 ADR、递增 compatibility epoch/major、更新 migration note、matrix、
   baseline 与 fixture，并让 CMake version selection 拒绝旧 compatibility request；
6. 紧急安全/正确性修复若必须绕过 deprecation 周期，要在 release note 和专门 ADR 中记录破坏范围与替代方案，
   不能静默作为 patch 发布。

### 5. 正式兼容承诺的进入条件

在本 ADR Accepted 之前，Tina 只能报告已通过的 package/consumer 事实，不能宣布正式 ABI。接受后也只有同时
满足以下条件的 release/tuple 才能标为 supported：

- compatibility matrix 与最低 OS/runtime/toolchain 基线已发布；
- exact release artifact、manifest、API/symbol baseline 可追溯；
- current consumer、previous-release source/object、package relocatability 和适用的 runtime/data probe 全部通过；
- `TinaConfigVersion.cmake` 行为与选定的 pre-1.0 版本方案一致，并有版本请求正反测试；
- breaking/deprecation/migration 记录完整，未覆盖的 C++ ABI 风险明确列出。

Ubuntu producer 到 Debian consumer 等跨发行版 gate 即使通过，也只为其实际 tuple 提供 fresh-consumer
artifact transfer 证据；在 previous-object probe 与 matrix 建立前，不升级为正式 ABI 结论。

## 结果

- SDK 发布从“能安装并运行”升级为版本、tuple、baseline 和 probe 可审计的兼容承诺；
- pre-1.0 仍可演进，但 breaking change 不再藏在 patch 或宽泛的 major-0 version match 中；
- 静态 C++、STL/PMR、模板/inline surface 带来的兼容限制被显式记录，consumer 不会误以为跨 toolchain 普遍兼容；
- 代价是每个 supported tuple 都要保存 artifact/baseline 并运行 previous-release probe，matrix 必须保持小而真实；
- 接受本 ADR 后还需单独实施 CMake version rule、release metadata、baseline 工具和 compatibility fixture；
  Proposed 状态本身不改变当前 `0.0.1` package 行为。

## 被拒绝方案

- 把 moved-prefix 或跨发行版 fresh consumer 当成 ABI 证明：没有复用旧对象文件，也未覆盖 tuple 变化；
- 只运行 `abidiff` 或只比较 symbol 文本：静态 C++ 的模板、inline、layout、allocator/exception ownership 和
  CMake usage requirement 可能没有充分 symbol 证据；
- 对外承诺“任意支持 C++23 的 compiler 都兼容”：C++ language level 不定义 compiler/STL/runtime ABI；
- 以 header-only/PImpl 重写所有公开 API 后才发布：会扩大当前任务并牺牲类型与性能设计；应先缩小 matrix、
  建 baseline，用实际 diff 决定哪些高变动边界需要 PImpl/C facade；
- pre-1.0 完全不做版本纪律：会让 SDK-001 consumer gate 无法转化为可维护的发布契约；
- 永久精确锁死所有 1.x patch/minor：避免了承诺，也让 SemVer 和 compatibility automation 失去价值。
