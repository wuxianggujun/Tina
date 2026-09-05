# Math

`Tina::Math` 是引擎中**唯一**的向量、四元数、矩阵与几何类型定义点。公共头位于
`include/tina/math`，不暴露 bx、GLM 或任何第三方类型。

它是 header-only `INTERFACE` target（形态同 `Tina::AssetTypes`）：全部入口都是值类型上的纯函数，
没有 owner、没有状态、没有分配，因此没有 `.cpp`、没有 `Create`、没有 `MemoryTag`。

## 当前能力

| 头 | 内容 |
| --- | --- |
| `Constants.hpp` | `Pi`/`TwoPi`/`HalfPi`、`DegreesToRadians`、`radians()`/`degrees()`、scale-relative `approximatelyEqual()` |
| `Vec.hpp` | `Vec2`/`Vec3`/`Vec4` 与算子、`dot`/`cross`/`crossZ`、`length`/`lengthSquared`/`distance`、`normalized`、`lerp`、`minimum`/`maximum`/`absolute`、`isFinite` |
| `Quaternion.hpp` | `Quaternion` 与 `operator*`、`conjugate`/`inverse`、`rotate`、`normalized`、`fromAxisAngle`、`slerp`、`isIdentity` |
| `Mat4.hpp` | 列主序 `Mat4`：`identityMat4`/`translationMat4`/`scaleMat4`/`fromQuaternion`/`fromTrs`、`multiply`、`transformPoint`/`transformDirection`/`transformVec4`、`transpose`、`determinant`/`linearDeterminant`、`inverse`，以及 `lookAtRightHanded`/`perspectiveRightHanded`/`orthographicRightHanded` 与 `ClipDepthRange` |
| `Geometry2D.hpp` | `Aabb2`、`Rect`：`contains`/`intersects`/`merge`/`expand`/`intersection`/`clamped`、`rotatedBounds`（保守旋转包围盒）、`toAabb2`/`toRect` |
| `Geometry3D.hpp` | `Aabb3`、`Sphere`、`Plane`、`Ray`、`RayHit`：`corners`/`transformed`/`emptyAabb3`、`signedDistance`/`projectOnto`/`nearestSignedDistance`、`makeRay`、`raycast`（三种形状） |
| `Frustum.hpp` | `Frustum`（6 个内向 `Plane`）：`frustumFromPerspective`、`frustumFromViewProjection`、`intersects(Sphere)`/`intersects(Aabb3)`/`contains`、`sphereIntersectsPerspectiveFrustum` |

**不在当前 Math 的能力：** `OBB`、`Mat3`/`Mat2`、双精度类型、SIMD 特化、`Transform` 复合类型、
曲线/样条、随机数、噪声、凸包与 GJK/SAT 碰撞。这些没有现有消费者，按下面的原则不预先发布。

## 五条设计约定

### 1. 失败用 `optional`/`bool`，不用 `Result`

`Core::Error` 构造即分配（ADR 0033 C4）。几何查询在剔除循环里每帧执行数千次，而"未命中"是
**正常结果不是失败**。因此命中查询返回 `std::optional<RayHit>`，谓词返回 `bool`，构造类
API（`makeRay`、`normalizedPlane`、投影矩阵）返回 `std::optional<T>`。

`Tina::Math` 因此**不占用 `ErrorDomain`**，也不修改 `Error.hpp`。需要结构化错误的边界（例如
`RenderSceneBuilder`）自己把 `nullopt` 翻成 `Core::Error`。

### 2. 退化输入 fail closed，且返回值可检测

- `normalized(Vec3{})` 与 `normalized(Quaternion{0,0,0,0})` 返回**零值**，不是 identity。
  identity 会把非法旋转静默变成合法旋转；零值可表示、可检测、并向下游传播为无效。
- `Mat4` 的 `inverse()` 对奇异或非有限矩阵返回 `nullopt`，不返回充满 inf/NaN 的矩阵 ——
  后者会静默污染所有派生值且调用方无从察觉。
- `lookAtRightHanded` 对退化基返回 `nullopt`，不回落到某个默认朝向：shadow cascade 悄悄
  对准错误方向比构建失败难查得多。
- `normalized` 的平方长度在 `double` 中累加，因此 float 平方下溢到零的向量仍可归一化。

### 3. 列主序 + 右手系，只有一套

`Mat4` 采用**列主序**：`element(row, column)` 位于 `columns[column * 4 + row]`，平移在
索引 12..14。这不是偏好 —— `RenderMesh3DItem::columnMajorWorldTransform` 已经是列主序并原样
交给 `bgfx::setTransform`，第二套约定意味着每个边界一次转置，以及一类只表现为镜像几何的缺陷。

投影全部右手系，与后端既有的 `bx::Handedness::Right` 一致。

**clip 深度范围是设备属性而非偏好**：OpenGL 系用 `[-1,1]`，D3D/Metal/Vulkan 用 `[0,1]`。
后端已从 device capabilities 读取该值并贯穿其 shadow math，因此 `Mat4` 的投影构造函数把它作为
`ClipDepthRange` 参数**显式接收**，而不是硬编码一个然后在一半平台上静默算错深度。

### 4. `Frustum` 平面法线内向

内向法线让剔除退化为一次符号检查，无需逐平面取反。`intersects` 是**保守**的：跨两个平面的
角落球体可能报告为可见却实际不相交。这是标准取舍 —— 精确测试的代价远高于偶尔多画一个物体。

`sphereIntersectsPerspectiveFrustum` 与 `intersects(Frustum, Sphere)` 代数等价，但**独立存在**
且**接收角度制**：它在 `double` 中累加（平面形式已舍入到 float）、不做逐项归一化，且
`RenderPerspectiveCamera` 存的就是度数 —— 在调用点转弧度会让角度先经过一次 float 舍入，足以
翻转恰好落在视锥边界上的球体，也足以改变已发布的剔除计数。

### 5. `Aabb2`/`Aabb3` 存对角，`Rect` 存位置+尺寸

两者大小相同、含义不同，因此转换必须**显式**（`toAabb2`/`toRect`）：隐式转换正是宽度被读成
x 坐标的来源。空盒（某轴 lower == upper）是**合法**的 —— 它是 merge 的单位元，也是完全裁掉
一个盒子后的正确结果。`emptyAabb3()` 返回反转盒作为累加单位元；用默认构造的盒子累加会错误地
把原点包含进去。

`rotatedBounds` 是**保守**而非精确的：结果包含旋转后的盒子但通常比其真实包围盒大。这正是
broadphase 与栅格化需要的保证 —— 漏掉一个 cell 是正确性缺陷，多覆盖一个不是。

## 与既有类型的关系

| 类型 | 状态 |
| --- | --- |
| `Scene::Vec3` / `Scene::Vec2` / `Scene::Quaternion` | **已删除**。`Scene::LocalTransform`/`WorldTransform` 现在持有 `Math::Vec3`/`Math::Quaternion` |
| `Physics2D::PhysicsVec2` | **已删除**，改用 `Math::Vec2`。`PhysicsAabb2D`/`PhysicsRayCast2D` 保留原名 —— 其 `Meters` 后缀字段承载 Box2D 单位契约，是物理语义而非纯几何 |
| `src/render/RenderScene.cpp` 的私有 `Vector3`/`Quaternion` | **已删除** |
| `src/scene/ExtractRenderScene.cpp` 的 `WorldBoundingSphere` 与球-视锥剔除 | **已删除**，改用 `Math::Sphere` 与 `Math::sphereIntersectsPerspectiveFrustum` |
| `src/scene/Animator3D.cpp` 的 4x4 乘法与 `shortestPathSlerp` | **已删除**，改用 `Math::multiply` 与 `Math::slerp` |
| `UI::UILogicalRect` | **保留**。它有自己的 logical-pixel 语义与 469 处引用（2026-09-01 复算口径：`grep -rn UILogicalRect src include tests samples editor`，按匹配行计数，不含 `tools/`；`tools/` 另有 1 处）；`Math::Rect` 只用于世界/屏幕矩形 |
| `Scene::CameraFollowPoint2D` | **保留**。是 2D 相机跟随的独立语义类型 |
| `bx::Vec3` / `bx::mtx*`（`src/render/bgfx/`） | **保留**。私有后端的实现细节，且已被 bgfx 自己的 `mtxLookAt`/`mtxOrtho` 消费；换成 `Math::Vec3` 只会在边界上来回转换 |

不做别名或兼容层：旧 API 直接删除、调用点一次改完（同 `AGENTS.md` 既有做法）。

## 数值等价性

替换既有实现时**逐字保持算式、中间精度与运算顺序**。产品证据数字（3D schema 16 的 mesh 提交、
product-2d schema 29 的 navigation blocker 计数等）由这些算式产生；改写会让"数字变了"与
"重构出错了"无法区分。

四个回归门禁把被删掉的实现原样复制进测试并逐元素比对：

| 测试 | 对照的已删实现 |
| --- | --- |
| `MathFrustumTest.StandaloneSphereTestMatchesTheReplacedExtractHelper` | `ExtractRenderScene.cpp` 的 `sphereIntersectsPerspectiveCamera` |
| `MathMat4Test.FromTrsMatchesTheReplacedHandWrittenBuilder` | `RenderScene.cpp` 的 `makeColumnMajorWorldTransform` |
| `MathQuaternionTest.SlerpMatchesTheReplacedAnimatorImplementation` | `Animator3D.cpp` 的 `shortestPathSlerp` |
| `MathGeometry2DTest.RotatedBoundsMatchTheReplacedPhysicsNavigationArithmetic` | `PhysicsNavigationSync2D.cpp` 的旋转保守 AABB |

**一处刻意的例外：** `RenderScene.cpp:1041` 保留自己的 `sphereIntersectsPerspectiveCamera`。它同时
返回 view depth（深度分桶与透明排序需要），且在 **float** 中累加而共享 helper 用 double。改成
double 会重新分类落在 float epsilon 内的球体并改变已发布 draw count —— 那是一次需要重新基线化的
决定，不是重构。

**别被同名函数误导（2026-09-05 补）：** `src/scene/ExtractRenderScene.cpp:185` 也有一个
`sphereIntersectsPerspectiveCamera`，但它是**薄 wrapper**，直接转调 `Math::sphereIntersectsPerspectiveFrustum`
（`:192`）。上表所说「`ExtractRenderScene.cpp` 的球-视锥剔除已删除」指的是那份**实现**已删，
函数名仍在。按名字搜会在两个文件命中，容易误判迁移没做完；真正的判据是函数体里有没有自己算平面。

`RenderScene.cpp` 的旧局部 `rotate` 用 twice-cross 优化，`Math::rotate` 用 sandwich 乘积：
代数等价但可能相差约 1 ULP。这是本轮唯一有意接受的舍入变化，由 3D 产品 gate 确认。

## 依赖方向

`Tina::Math` 只依赖 `Tina::Core`（取固定宽度类型）。它不依赖 Render、Scene、Physics 或
Platform，因此可被任意模块使用而不引入循环。

链接可见性按公共面区分：`Tina::Scene`、`Tina::Physics2D`、`Tina::Asset`、`Tina::Gameplay2D`、
`Tina::Animation3D`（`src/animation3d/CMakeLists.txt:39`）与 `Tina::Gameplay`（`src/gameplay/CMakeLists.txt:34`）
以 **PUBLIC** 链接（其公共头命名 `Math::` 类型；后两个是 2026-09-05 补录，此前漏列）；`Tina::Render` 以 **PRIVATE** 链接 ——
它的公共头仍发布扁平 float 字段，`Math` 只出现在 `RenderScene.cpp` 内部，声明为 PUBLIC 会
宣告一个契约上并不存在的依赖。

## 验证

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_math_tests tina_scene_tests tina_render_scene_tests tina_physics2d_tests `
  --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_math_tests.exe --gtest_color=yes
```

每个公开头有一个 `tests/math/header_isolation/*Header.cpp` 单 TU，只 include 自己并
`static_assert` 已发布契约（大小、trivially copyable、默认值、列主序索引、平面顺序、返回类型），
因此靠 sibling include 才能编译的头会在此失败而不是在消费方构建时失败。
