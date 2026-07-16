# ADR 0014：EngineHost 阶段 Context + IGame/AppStateStack

- 状态：Proposed
- 日期：2026-07-16

## 背景

旧 Application 同时充当全局服务入口、模块 owner 和主循环；Scene 又形成第二套生命周期。
单个通用 EngineContext 会把 Service Locator 换一个名字，二值 pause/resume 也无法表达“停止
Fixed/Input 但继续 Render”的覆盖状态。

## 推荐决定

EngineHost 是唯一非全局 owner，通过短生命周期 phase Context 暴露最小能力。IGame 是永久
bottom frame client，Runtime 独占 overlay AppStateStack；StatePolicy 分别控制 Gameplay/UI
Input、Fixed、Variable、Render 向下传播。State enter 是事务，exit `noexcept` 且恰好一次；
push/pop/replace/policy change 只在 Deferred Cleanup 提交，不保留并列 SceneManager 或二值
onPause/onResume。

## 代价

- Phase Context/State transaction 需要更多小类型和失败注入测试；
- 旧 Scene 生命周期必须迁移，不能机械套壳；
- 调用者无法随时拿到所有 Engine service，这是有意约束。

## 被拒绝方案

- 全局 Application/Service Locator/保存 EngineContext：隐藏依赖和借用寿命；
- IGame 与 SceneManager 两套平行栈：输入、渲染和退出顺序会分叉。
