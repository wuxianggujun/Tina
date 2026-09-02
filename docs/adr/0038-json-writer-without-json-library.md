# ADR 0038：自研 JsonWriter，不引入 JSON 库，不实现 JSON 解析

- 状态：Accepted
- 日期：2026-09-01

## 背景

样例、编辑器和 bench 工具把诊断报告以单行 JSON 写到 stdout/stderr，由
`tools/windows/*.ps1` 与 `tools/bench/run_benchmark_gate.py` 消费。这条路径此前没有任何
写出抽象，事实形态是两点：

1. 一个转义函数 `writeJsonString(std::ostream&, std::string_view)` 被复制了 13 份
   （`editor/app/EditorFileDialogGateMain.cpp`、
   `editor/app/EditorWorkspaceState.hpp`、`tools/bench/UIBenchmarkWorkloads.cpp`
   和 10 个 `samples/*/main.cpp`），共 66 个调用点。这 13 份**并不一致**，而是三个版本
   （按 `git show HEAD:<file>` 逐份核对函数体所得）：

   | 版本 | 份数 | `"` `\` | `\n` | `\r` `\t` | 其余 `< 0x20` |
   | --- | --- | --- | --- | --- | --- |
   | 完整转义 | 8 | 转义 | 转义 | 转义 | `\u00xx` |
   | 缺 `\r` `\t` | 3 | 转义 | 转义 | **丢弃** | **丢弃** |
   | 只转义两个字符 | 2 | 转义 | **丢弃** | **丢弃** | **丢弃** |

   完整转义那 8 份是 `desktop`、`desktop_shell`、`null`、`platform`、`ui_showcase`
   五个样例加上两个 editor_app 文件与 bench；缺 `\r` `\t` 的是 `2d_bgfx`、`3d_bgfx`、
   `3d_product`；只转义两个字符的是 `2d_catalog`、`2d_tilemap_bgfx`。后两个版本产出的 JSON
   合法但静默丢数据，而复制本身掩盖了这个分歧：读任何单独一份都看不出还有另两种行为。
2. 对象结构由裸字符串字面量拼接而成。`samples/2d_tilemap_bgfx/main.cpp` 的成功报告是一条
   跨 355 行、包含 339 个 `<<` 的单语句；全仓库此类 `<<` 片段约 1809 处、分布在 29 个文件。

代价不在字符数，而在失效模式：漏一个逗号、漏一个引号、把 `,` 写在 `{` 之后，都能通过编译，
只在 gate 抓不到证据时才暴露，且报错位置与真因无关。

消费侧的格式约束是硬契约，先按字节选行再结构化取值：

- 6 个 gate 用锚定正则选行，要求 `status` 第一、`sample` 第二且紧邻，例如
  `RunProduct2dGate.ps1:325` 的 `'^\{"status":"ok","sample":"tina_sample_2d_authored_scene"'`；
- `RunProduct2dGate.ps1:148-280` 约 130 条裸 key 正则形如 `'evidenceSchema\":29'`，
  冒号后不允许空格；
- `run_benchmark_gate.py:119` 要求恰好一行非空 JSON。

读取侧不需要新能力。C++ 中唯一的 JSON 解析是 glTF，由 `thirdparty/cgltf/cgltf.h` 内置的
jsmn 承担，已由 ADR 0009 认可；`tools/windows/baselines/*.json` 与
`tools/bench/profiles/*.machine-profile.template.json` 只被 PowerShell 和 Python 读取。
资产 recipe 是自定义的行式、空格分词、动词开头 DSL（`src/asset/CatalogCook.cpp:1935`），
与 JSON 无关。

## 决定

在 `include/tina/core/text/JsonWriter.hpp` 增加一个仅写出的 `Tina::Core::JsonWriter`，
与 `ParseFloat.hpp`/`Utf8.hpp` 同目录同风格：header-only、全部 `noexcept`、无分配、
无异常、无虚函数。它包装调用方持有的 `std::ostream&`，按插入顺序写出紧凑 JSON，
自行维护逗号与括号配对，并在内部 `TINA_ASSERT` 上检查嵌套配对。

不引入任何第三方 JSON 库，也不实现 JSON 解析器或 DOM。

数值格式化委托给被包装的 `std::ostream`，不自行格式化，因此迁移后的数值输出与迁移前逐字节一致。

字符串转义统一到**完整转义**那一版：控制字节写成 `\u00xx`，非法 UTF-8 字节原样透传（不替换、
不校验，需要校验时调用方另行使用 `Core::isStrictUtf8`）。这对上表后两个版本共 5 个样例
（`2d_bgfx`、`3d_bgfx`、`3d_product`、`2d_catalog`、`2d_tilemap_bgfx`）是一次**有意的行为变更**，
不是逐字节兼容：丢弃控制字节会静默丢数据，而 JSON 本就要求转义它们。已核对迁移前的
`tina_sample_2d`、`tina_sample_null`、`tina_sample_platform`、`tina_sample_2d_catalog`、
`tina_sample_3d` 实际输出，其中控制字节数为 0，因此这些 gate 的输出仍逐字节不变；变化只会在
某个错误 message 真的含控制字节（或 `\r`、`\t`）时显现，那时新行为才是对的。

13 份 `writeJsonString` 副本与全部 66 个调用点一次性迁移到该类型并删除，不留别名、包装或
双轨分支（`AGENTS.md` `## 核心约定`）。

迁移范围是**全部**手写 JSON 发射点，不止使用过 `writeJsonString` 的那些：以 `writeJsonString`
为搜索锚点会漏掉两类，都已一并迁移。一类是从未用过该 helper 的 15 个文件
（`samples/2d`、`2d_authored_scene`、`2d_tilemap`、`3d`、`asset`、`gallery`、`network`、
`physics2d_bench`、`virtual_stick`，以及 `tools/assetc`、`tools/catalog_validate`、
`tools/bench/main.cpp`）；另一类更隐蔽——`samples/3d_product/main.cpp` 与
`samples/2d_tilemap_bgfx/main.cpp` 的**验证失败报告**是与成功报告并列的另一条独立 `<<` 链
（183 行 / 272 行），迁移成功报告时不会碰到它。

`samples/network/main.cpp` 原本不是 `<<` 链而是单条 `std::fprintf`，18 个可变参数与格式串
分离手工对齐——参数错位是未定义行为且编译期无警告，这是本轮收益最大的一处。其
`writeEvidence(std::FILE*, ...)` 相应改为 `writeEvidence(std::ostream&, ...)`。

两处**有意不迁**：

- `src/core/diagnostics/CrashHandler.cpp` 的 JSON trailer。该文件的 `emit` 同时写 stderr 和一个
  Win32 `HANDLE`，并注明 `WriteFile, not fputs: the CRT may be unusable by the time we get here`。
  `JsonWriter` 包装 `std::ostream`，接进来只能先经 `ostringstream`，即在堆可能已损坏的崩溃路径上
  引入一次堆分配。已核对该 trailer 的 `reason` 全部来自字符串字面量（`writeReport` 的 8 个调用点
  与 `describeSehCode` 的 8 个 return），不含引号或反斜杠，故缺少转义在此不可达。
- `tools/bench/UIBenchmarkWorkloadsTests.cpp` 的 171 处。其中 120 处是
  `EXPECT_NE(first.find("\"nodes\":{\"requested\":1024,...\"}"), npos)` 形式的**期望字节基线**，
  用 `JsonWriter` 生成期望值等于让被测代码自证，测试将永远通过、不再能发现格式回归；其余是
  `extractChecksum`/`extractQuotedField` 两个**读** JSON 的辅助函数，属消费侧，只写的
  `JsonWriter` 无法替代。

## 结果

- 逗号、引号、括号配对由类型维护，写错结构从"gate 运行时失败"变为"不可能表达"；
- 转义逻辑只有一处，修一次即全仓库生效；
- key 顺序即插入顺序，锚定 `status`/`sample` 的 6 个 gate 继续成立；
- 迁移必须以逐字节输出比对验证，而不是只看 gate 通过：先取迁移前基准输出，再比对；
- 若将来 C++ 侧真的需要读 JSON（非 glTF），那是独立决定，需要新增 ADR，本 ADR 不预留接口。

## 被拒绝方案

- **引入 nlohmann/json**：默认底层为 `std::map`，按字典序重排 key，`sample` 会排到 `status`
  之前，6 个锚定 gate 全部失效，且是运行时失效而非编译错误。改用 `ordered_json` 等于为本仓库
  的格式契约给第三方库做特化；其 DOM、解析器与默认异常语义也都不是本路径所需，与
  "边界返回 `Result`/`Status`" 的既有风格冲突。
- **引入 RapidJSON**（cocos2d-x 的做法）：同样带入 DOM、解析器与 schema。cocos2d-x 自身
  即是反例：`external/json` 之外，spine 另带一份 cJSON 系 `Json.h`，一个仓库两套 JSON 实现。
- **自研完整 JSON 库（含解析器与 DOM）**：没有消费者。唯一的 JSON 读取需求是 glTF，
  已由 ADR 0009 的 cgltf 覆盖，重写它只会新增一个必须自行维护的攻击面与正确性负担。
- **保留 13 份副本，只抽出转义函数**：不解决真实缺陷。逗号与括号配对仍由手写字面量承担，
  失效模式不变。
- **改为 pretty-print 或按字典序输出**：直接违反消费侧契约（冒号后无空格、`status`/`sample`
  锚定）。
