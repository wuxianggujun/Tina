# ADR 0051：整形文本、按需 MSDF、统一布局约束与组合背景

> 容量政策状态：本文将固定容量作为普遍约束的部分已由 [ADR 0052](0052-demand-driven-memory-policy.md) 部分替代（2026-09-07）。原有 owner、借用、事务、布局和交互决定继续有效；下文保留历史理由，当前代码迁移状态见内存策略文档。

- 状态：Accepted
- 日期：2026-09-07
- 决策依据：本轮 maintainer 明确要求 MSDF + HarfBuzz、多语言回退、约束传递与 UIPanel 组合；代码全部完成后由主会话直接统一编译测试。
- 补充 ADR 0011/0022/0023；替代其历史 R8 位图文本切片和复杂 shaping 后置状态，不改变 retained owner、事务、行为与资源寿命的决定。

## 决定

1. `Tina::UIFreetype` 的真实文本路径使用 HarfBuzz OpenType 整形、FriBidi 段落双向级别、FreeType 字体/轮廓和彩色字形读取，以及 msdfgen 多通道距离场。上述依赖全部 PRIVATE。FreeType 保留作为轮廓读取器，不再把普通轮廓先栅格成 R8 再放大。
2. `TextShaper` 拥有复制的字体字节和字体对象，输出视觉顺序 `GlyphRun`。字形索引、UTF-8 cluster byte range、advance、offset 与逻辑 scalar caret map 分开，禁止假定“一 Unicode 字符等于一个字形”。相同 UI 字符串使用有界 intern cache；字体链变化使 cache 失效。
3. 明确配置回退链；按完整 grapheme 选字体，并在每个 script / direction / face run 上整形。彩色 Emoji 走 FreeType 支持的 BGRA bitmap/color-layer 路径，转换为 premultiplied RGBA；MSDF 不承担彩色图像。字体链仍缺字时使用该字体 `.notdef` 并计数，容量/格式错误不能假装成缺字。
4. 轮廓固定使用 48 texels/em、4 texel distance range 的 MSDF；运行时按 `(face generation, glyph index, raster size)` 缓存，普通轮廓与字号/DPI 解耦。CPU 图集 RGBA8、有界 shelf packing、一 texel gutter、hash lookup；不遍历字体 charmap、不预生成全部 CJK、不在已有 committed UV 存活时偷偷重排。
5. `tina_msdfgen` 是共享同一实现的 host 预处理工具。CMake 经 UTF-8 Python 脚本把 UI string manifest 烘焙为 PNG + JSON，并额外生成 versioned `.tmsdf` cooked cache seed。运行时读 `.tmsdf`，不为文本再加 PNG/JSON decoder；PNG/JSON 用于检查与其他资源工具。schema 不匹配拒绝，不嗅探旧格式。
6. UI→Render 保留字形精确浮点四角，整数 AABB 仅供裁剪。Pixel snap 只作用于整行 origin/baseline，不分别 round 每个字形的 advance 或宽高。Shader 用 RGB median 和 UV derivatives 恢复 coverage；距离纹理不能标成 sRGB，彩色字形不能重复 premultiply 或被黑色文字 tint 染黑。
7. `UILayoutAxisConstraint` / `UILayoutConstraints` 统一未定与已定轴：未定最大值为 infinity，tight 轴提供百分比基准。viewport 只在根 prepare 时注入；Measure 与 Arrange 复用同一 border-box resolver，不保留 `NoFallbackCount` 平行算法和子节点 viewport 兜底。Flex/Grid/Overlay/Scroll 的不同容器算法继续解释同一约束，不是兼容分支。
8. `UIPanel` 是 `UIImageSource + optional UINineSlice + tint/sampling` 的值组合，不是 Button 的父类。`UIElementVisual::panel` 在 create 时复制为首条 bounded Canvas 命令，`ElementBorderBox` 只在 paint 时读取最终盒子，不参与 intrinsic measurement。继续复用 NineSlice→ImageQuad、root resolver 和 FramePin，不取得 GPU owner，也不改交互/focus/semantics。

## 容量与失败

- 默认动态图集 2048×2048 RGBA8（16 MiB）、4096 个 slot；尺寸/slot 在 `UIContextCapacityConfig::glyphAtlas` 配置。slot 上限不保证所有尺寸的 4096 字形都能装下，先耗尽像素预算也会失败。
- 默认 glyph image cache 16 MiB / 4096 个唯一字形；单次最多 4096 glyph/scalar、64 KiB UTF-8；8 个 face，每个字体最多 64 MiB；单张字形最多 512×512，复杂轮廓点数有上限。第三方 cold-miss 工作也受这些输入上限约束。
- 图集满时返回 `CapacityExceeded`，旧 committed paint 保留；不清空在途页面，不整段替换为无声实心方块。当前不自动 LRU eviction；产品可配置预算或在窗口/font owner 边界重新创建。
- 字体 seed 先完整校验版本、font fingerprint、glyph index、像素范围、采样种类和预算，再写 cache。它是缓存输入而非 font/shaping 的替代品。
- line/glyph cache、Atlas、UI snapshot 与 GPU upload 是不同 owner；`GlyphRun`/raster spans 不能跨下一次 measure/raster/font mutation 保存。

## 证据与边界

见 [渲染诊断与交付报告](../ui-text-msdf-report.md)。自动测试证明数学/容量/接口，不代替各语言母语排版审阅和真机 IME。UAX #14 词典断行、COLRv1 paint graph、OpenType-SVG rasterizer 与 font pack 发行不因整形库接入而自动存在。
