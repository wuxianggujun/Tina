# ADR 0035：`Tina::Math` 作为唯一几何类型定义点

- 状态：Accepted
- 日期：2026-08-30
- 决策者：Tina maintainers

## 背景

在本 ADR 之前，Tina 没有 math 模块：`namespace Math`、`tina/math`、`Mat4`、`Frustum`、`Ray`、
`Plane`、`OBB` 在 `include/` 与 `src/` 全部零命中。存在的几何代码是**分散且重复**的：

| 位置 | 内容 |
| --- | --- |
| `include/tina/scene/Transform.hpp:10,43` | `Scene::Vec3`、`Scene::Quaternion` 与 8 个自由函数 |
| `include/tina/scene/SpriteRenderer2D.hpp:10` | `Scene::Vec2` |
| `include/tina/physics2d/PhysicsTypes.hpp:11` | `PhysicsVec2`（全仓 52 处引用） |
| `src/render/RenderScene.cpp:19,25` | **第三份** `Vector3` + `Quaternion`，含自写 `dot`/`cross`/`add`/`multiply`/`rotate` |
| `src/render/RenderScene.cpp:114` | 手写 TRS → 列主序矩阵 |
| `src/scene/ExtractRenderScene.cpp:94,120` | 局部 `WorldBoundingSphere` 与球-视锥剔除 |
| `src/scene/Animator3D.cpp:95,113` | 手写 4x4 乘法与 `shortestPathSlerp` |
| `src/asset/PhysicsNavigationSync2D.cpp:160` | 手写旋转保守 AABB |
| `samples/3d_product/core/Product3DUI.cpp:37`（撰写本 ADR 时为 `samples/3d_product/Product3DUI.cpp`） | 局部 `struct Rect`（已迁至 `Math::Rect` 别名） |

三份独立的 `Quaternion` 定义是本 ADR 的直接动因：它们不能互相传递，每个边界都要逐字段重装，
而 `rotate` 的两种实现（sandwich 乘积 vs twice-cross）已经产生了约 1 ULP 的差异。

参考实现调研：cocos2d-x 有完整的 `cocos/math`（`Vec2/3/4`、`Mat4`、`Quaternion`、`AABB`、`OBB`、
`Ray`、`Plane`、`Frustum`）。它作为成熟工程经验来源，不作为移植源（见 `docs/carbon-reference.md`
的使用规则）。

## 决策记录

| # | 决策点 | 采纳 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 模块形态 | **header-only `INTERFACE` target** | 静态库：全部入口是值类型上的纯函数，无状态无分配，没有任何内容可放进 TU。形态复用 `Tina::AssetTypes` 既有先例 |
| D2 | 失败表达 | **`optional`/`bool`，不引入 `ErrorDomain`** | `Result<T>`：`Core::Error` 构造即分配（ADR 0033 C4），而剔除循环每帧数千次调用且"未命中"是正常结果。附带收益：不触碰共享 `Error.hpp`，与并行推进的模块零冲突 |
| D3 | 类型范围 | **只发布有真实消费者的类型；不做 `OBB`** | 照 cocos2d-x 全量补齐：会重演 ADR 0034 的「已发布无生产者」缺陷（`nativeBindingRevision` 落地时从未被赋值）。`OBB` 连测试外的用途都不存在，需要时再加 |
| D4 | 矩阵布局与手性 | **列主序 + 右手系，唯一一套** | 行主序：`RenderMesh3DItem::columnMajorWorldTransform` 已是列主序并原样进 `bgfx::setTransform`，第二套约定等于每个边界一次转置，且缺陷只表现为镜像几何 |
| D5 | clip 深度范围 | **显式 `ClipDepthRange` 参数** | 硬编码 `[0,1]` 或 `[-1,1]`：深度范围是**设备属性**（OpenGL 系 `[-1,1]`，D3D/Metal/Vulkan `[0,1]`），后端已从 device caps 读取并贯穿其 shadow math。硬编码会在一半平台上静默算错深度 |
| D6 | bgfx 后端是否改用 `Math` | **不改，继续用 `bx::Vec3`** | `src/render/bgfx/` 是私有后端，`bx::math` 已被 bgfx 自己的 `mtxLookAt`/`mtxOrtho` 消费；替换只会在边界上来回转换。这是有意的边界，写进文档而非留给读者猜 |
| D7 | 数值等价性 | **逐字保持算式、精度与运算顺序** | 顺手统一精度：产品证据数字由这些算式产生，改写会让「数字变了」与「重构出错了」无法区分 |
| D8 | 旧类型处理 | **直接删除，不留别名** | 别名 + 逐步迁移：`AGENTS.md` 的既有做法是删除旧 API、一次改完调用点；别名会让两套名字长期并存，正是本 ADR 要消除的状态 |
| D9 | `PhysicsVec2` | **删除，改用 `Math::Vec2`**；`PhysicsAabb2D`/`PhysicsRayCast2D` **保留原名** | 全部改成 `Math::Aabb2`/`Ray2`：那两个类型的 `Meters` 后缀字段承载 Box2D 单位契约，是物理语义而非纯几何 |
| D10 | `UI::UILogicalRect` | **保留，不动** | 统一成 `Math::Rect`：它有独立的 logical-pixel 语义和 469 处引用，与世界/屏幕矩形不是同一个概念 |

## 决定

### 1. 唯一定义点

`include/tina/math` 是引擎中向量、四元数、矩阵与几何类型的唯一定义点。任何模块不得再定义
自己的 `Vec2`/`Vec3`/`Quaternion`/矩阵/包围盒，包括匿名 namespace 内的"临时"副本 —— 三份
`Quaternion` 正是这样积累的。

### 2. 退化输入 fail closed，且可检测

`normalized()` 对退化输入返回**零值而非 identity**：identity 会把非法旋转静默变成合法旋转，
零值可表示、可检测、并向下游传播为无效。`Mat4::inverse()` 对奇异矩阵返回 `nullopt` 而非充满
inf/NaN 的矩阵；`lookAtRightHanded` 对退化基返回 `nullopt` 而非回落到默认朝向。

理由一致：一个悄悄给出貌似合理结果的失败，比一个明确拒绝的失败难查得多。shadow cascade 对准
错误方向不会崩溃，只会画错。

### 3. 保守几何的保守性是契约的一部分

`rotatedBounds` 与 `Frustum::intersects` 是**保守**的：前者的结果通常大于真实包围盒，后者可能
把角落球体判为可见。这是被依赖的性质而非缺陷 —— 栅格化漏掉一个 cell 是正确性缺陷，多覆盖一个
不是。公开注释与单测都固定这一点，防止后续"收紧"成精确测试而破坏包含保证。

### 4. 数值等价性由回归门禁保证

四个被删除的实现原样复制进测试并逐元素比对（见 `docs/math.md` 的表）。这不是重复代码，而是
D7 的可执行证据：任何一处算式漂移会在此失败，而不是在产品 gate 里表现为无法解释的计数变化。

**一处刻意的例外并保留两份实现：** `RenderScene.cpp` 的 `sphereIntersectsPerspectiveCamera`
不改用共享 helper。它同时返回 view depth（深度分桶与透明排序需要），且在 **float** 中累加而
共享 helper 用 double。改成 double 会重新分类落在 float epsilon 内的球体并改变已发布 draw
count —— 那是需要重新基线化的决定，不是重构。这个例外写在代码注释里，不藏在文档中。

### 5. 链接可见性按公共面区分

`Tina::Scene`/`Physics2D`/`Asset`/`Gameplay2D` 以 **PUBLIC** 链接 `Tina::Math`（其公共头命名
`Math::` 类型）；`Tina::Render` 以 **PRIVATE** 链接 —— 其公共头仍发布扁平 float 字段，`Math`
只出现在 `RenderScene.cpp` 内。声明为 PUBLIC 会宣告一个契约上并不存在的依赖。

### 6. `Tina::Math` 不占用 `ErrorDomain` 与 `MemoryTag`

它没有 owner、不分配、不持有状态，因此两者都不需要。这条同时是 D2 的附带收益：`Error.hpp` 与
`MemoryTag.hpp` 是高频冲突的共享头，本模块完全不触碰。

## 结果

- 三份 `Quaternion`、两份 `Vec3`、两份 `Vec2` 归并为一份；`Mat4`/`Ray`/`AABB`/`Plane`/`Frustum`/
  `Rect`/`Sphere` 首次存在。
- 每个新类型在本切片内即有真实消费者，无「已发布无生产者」项。唯一边界情况是 `Ray`：它当前的
  消费者只有单测与 `Frustum`/`Aabb3`/`Plane` 求交的对偶关系，Editor 3D 视口拾取仍走「投影到屏幕
  再比 2D 包围盒」（`EditorWorkspaceGizmo.cpp`），改用 `Ray` 拾取是独立切片。这一点明确记录，
  不假装它已经接线。
- 成本与限制：
  - **`OBB` 缺失**（D3）。需要精确旋转盒相交或 SAT 时须新增，并同时提供消费者。
  - **无 SIMD、无双精度类型、无 `Mat3`/`Mat2`。** 当前没有 profile 证据表明标量实现是瓶颈。
  - **`RenderScene.cpp` 仍保留一份 float 精度的球-视锥测试**（第 4 节），因此该分类在两处存在
    两种精度。这是已知且有意的，不是遗漏。
  - **`rotate` 的实现从 twice-cross 换成 sandwich 乘积**，可能相差约 1 ULP。这是本轮唯一有意
    接受的舍入变化。
  - **`bx::Vec3` 仍在 bgfx 后端内**（D6），因此该目录不受本 ADR 的「唯一定义点」约束。
- 已建立的门禁：`tina_math_tests`（六个测试文件 + 七个 header-isolation 单 TU，含 D7 的四个
  等价性回归）；`VerifyInstalledTinaSdkHeaders.cmake` 的第三方 token 扫描覆盖新增安装头
  （`Tina::Math` 只依赖 `Tina::Core`，无第三方面）。

## 被拒绝方案

- **引入 GLM / Eigen / DirectXMath**：会把第三方类型带进 `Scene`/`Physics2D` 的公共头，直接违反
  「Game SDK 不暴露第三方类型」；且 ADR 0007 已确立标准库优先、第三方保持私有。本模块的全部内容
  是纯函数与 POD，自持成本远低于一个公开 ABI 依赖。
- **复用 `bx::Vec3` / `bx::mtx*` 作为引擎级类型**：`bx` 是 bgfx 的实现细节，会让 Scene 无法在
  不启用 bgfx 后端时编译，也会在安装头里泄漏 `bx` token。
- **移植 `cocos/math`**：cocos2d-x 本体为 MIT 允许复制，但本 ADR 只借鉴取舍不移植代码
  （同 ADR 0033 第 9 节）。其 `Mat4` 另有一套自己的手性与行主序约定，与 D4 冲突。
- **保留 `Scene::Vec3` 作为 `Math::Vec3` 的别名**：见 D8。别名让两套名字长期并存，而消除这种
  并存正是本 ADR 的目的；用户明确要求清除旧用法而非叠加一层。
- **把 `UI::UILogicalRect` 并入 `Math::Rect`**：见 D10。两者语义不同，合并会把 logical-pixel
  契约稀释成通用矩形。
- **让几何查询返回 `Result<T>`**：见 D2。每帧数千次调用的路径上，"未命中"不是错误，而
  `Core::Error` 构造即分配。
- **顺手统一 float/double 精度**：见 D7 与第 4 节。产品证据数字由现有算式产生，精度统一是独立的
  重新基线化工作。
