# ADR 0001：完整 vNext 目标，通过垂直切片迁移

- 状态：Accepted
- 日期：2026-07-16

## 背景

当前 `Application`、Scene、World、Render、Asset 和 UI 边界互相泄漏。继续局部修补会固化
全局组合根；一次提交替换全部实现又会长期失去可构建、可运行证据。

## 决定

允许 vNext 不兼容旧 API/资源/场景，但先冻结完整目标，再按 Null Runtime、Platform/UI、
Scene/2D、Render/3D、Asset/Cooker、Product UI/Audio 的垂直切片迁移。每个切片必须构建、
直接 GoogleTest、运行对应 sample、验证资源归零并独立提交。Legacy 只在替代能力通过门禁后
按模块删除。

## 结果

- 目标不会被旧接口限制；
- 任意阶段仍有可运行证据和回滚点；
- 双架构期间需要严格禁止新 target 依赖 Legacy；
- 迁移速度由门禁质量而不是一次提交大小衡量。

## 被拒绝方案

- 永久围绕旧 Application 修补：无法形成模块边界；
- Big-bang 单提交：失败难定位且长期不可运行。

