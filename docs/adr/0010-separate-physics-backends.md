# ADR 0010：Box2D/Jolt 分离，不强行统一物理 API

- 状态：Accepted
- 日期：2026-07-16

## 背景

2D 与 3D 物理在形状、约束、查询、求解和坐标语义上并不相同。为了表面统一而建立一套
最低公分母接口，会隐藏后端能力并增加转换成本。

## 决定

2D 物理使用独立 `tina_physics2d` 和 Box2D 3.x；真实 3D 玩法需要后再建立
`tina_physics3d` 并接入 Jolt。两者可以共享 Core 数学/ID/错误规范，但不共享强行统一的
World、Body、Shape 或 Query API。首批 vNext 切片不启用物理。

## 结果

- 2D 和 3D API 可以忠实表达各自能力；
- Scene 通过同步系统交换 Transform/事件，不持有第三方类型；
- Jolt 版本、线程和确定性策略需在接入前单独补充 ADR；
- 不增加第三套物理 backend。

## 被拒绝方案

- 一个 `IPhysicsWorld` 同时抽象 2D/3D：接口会退化为最低公分母；
- 首期同时接入 Box2D/Jolt：没有首个垂直切片消费者，违反 YAGNI。
