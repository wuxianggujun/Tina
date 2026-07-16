# ADR 0007：vNext 不使用 EASTL，xxHash 保持私有

- 状态：Accepted
- 日期：2026-07-16

## 背景

EASTL 在可控 allocator、固定容量容器和游戏主机环境中有价值，但“库的理论性能最好”不能
证明 Tina 当前调用点更快。复制 EASTL 子集会让项目自行承担 allocator、异常安全、迭代器、
ABI 和跨平台维护。xxHash 解决的是高速非密码学 Hash，并不依赖 EASTL 的保留与否。

## 决定

vNext 通用容器使用标准库和 `std::pmr`，不 include/link EASTL/EABase，也不重写 Tina STL。
只有出现明确消费者、容量和溢出策略时，才实现少量引擎专用结构，例如 `StaticVector`、
`InlineFunction`、`GenerationPool` 和 `FrameArena`。xxHash 通过私有 adapter 提供版本化
`ContentHash`，不进入公共类型，也不承担安全签名或对象唯一身份。

## 结果

- 优化针对数据布局、分配次数和访问模式，而不是容器品牌；
- Legacy 可在迁移期继续使用 EASTL，零引用后连同 EABase 删除；
- 专用容器必须有 benchmark、边界测试和真实调用点；
- 不可信资产的完整性需要单独的密码学校验方案。

## 被拒绝方案

- vNext 继续全面使用 EASTL：扩大公共依赖和迁移面；
- 复制 EASTL 的“常用部分”：形成未经长期验证的自研 STL；
- 删除 xxHash：会无理由放弃 Cooker cache/content hash 的成熟实现。
