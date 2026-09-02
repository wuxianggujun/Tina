# ADR 0043：Mesh3D 材质增加 emissive factor

- 状态：Proposed
- 日期：2026-09-02

## 背景

`samples/3d_voxel` 要在天上画一个能看见的太阳。ADR 0042 让天空颜色随太阳变，但清屏色只能把
整块屏涂成一个颜色，画不出一个有位置、有边缘的圆盘。圆盘只能是几何体。

问题在于当前的 3D 光照模型画不出它。`fs_tina_opaque3d_mr.sc` 的最终颜色是：

```
lit  = Σ shadeLight(...)                  // 每个 directional/point/spot
lit += albedo * ambientScale * (...)      // 或者 IBL
gl_FragColor = vec4(linearToSrgb(lit), a)
```

每一项都是 `albedo × 光照`，**没有一项与光照无关**。太阳圆盘朝着相机，而太阳光从它背面照过来，
`NdotL < 0`，所有 direct light 项归零，它只剩 ambient。demo 当前 `AmbientScale = 0.09`，
而正午天空的线性亮度是 0.34 —— 太阳会比它所在的天空更暗。

这不是 demo 能绕开的：
- 调大 `baseColorFactor`（校验只查 finite，不限 `[0,1]`，确实能给 11.0）可以把 ambient 项撑亮，
  但太阳亮度就绑死在 `ambientScale` 上，ambient 一改太阳就跟着变，是个隐藏耦合；
- 给圆盘一个朝向太阳的法线可以拿到 direct light，但那样它就成了被自己照亮的物体，
  背对相机、且亮度随自己的朝向变；
- 关掉光照单独画一个 pass 要碰被冻结的 `RenderPassKind`。

引擎整棵树（`include/`、`src/`、shaders）当前 **零 emissive**：grep `emissive` 只命中
`thirdparty/cgltf`。也就是说这是一个真实的能力缺口，不是 API 摆放问题。

## 决定

`Mesh3DMaterialBindingDesc` 增加 `emissiveFactorR/G/B`（默认 0，即现有材质行为不变）。
后端打成新 uniform `u_emissiveFactor`，fragment shader 在 ambient/IBL 之后、`linearToSrgb`
之前加一项 `lit += emissive`。

放在 **material binding 而非 per-instance**，理由有三：

- glTF 2.0 把 `emissiveFactor` 定义在 material 上，与已在此处的 `metallicFactor`/
  `roughnessFactor` 同级；
- per-instance 要占 instance data 的新槽位，那会改顶点布局、stride 与 batching，
  而 emissive 的自然粒度就是材质；
- 静态/骨骼/透明三条路共用 `fs_tina_opaque3d_mr`，uniform 一处改动全覆盖，
  instance data 则要分别处理静态与骨骼两个 vs。

校验取 **finite 且非负，但不设上限**。emissive 是 radiance，与 `Mesh3DDirectionalLight::colorR`
同一标尺；那里也不限上限。`metallicFactor`/`roughnessFactor` 限 `[0,1]` 是因为它们是 BRDF 的
无量纲参数，不是同类东西。太阳需要远大于 1 的值才能在 sRGB 编码后压到接近白。

## 结果

- 自发光材质成为公开能力：太阳、月亮、灯泡、熔岩、发光矿石、屏幕，都不再需要碰后端；
- 与光照解耦：emissive 不乘 `NdotL`、不乘 ambient、不受阴影影响，所以一个背光的面也能是亮的。
  这正是太阳圆盘需要的性质；
- **不含 emissive 贴图**。只有 factor，即整个材质均匀自发光。贴图要再占一个 sampler 槽
  （当前 opaque3d 已用到 15 个：color/MR/normal/CSM/IBL×3/spot/point×6），且要走 cooked
  asset schema 的版本升级。另开切片；
- 默认 0 意味着这是**加性的、不改变任何现有材质**的扩展。没有兼容层，因为不需要；
- HDR 的老问题在这里露头：shader 末尾是 `linearToSrgb(lit)` 而**没有 tone mapping**，
  所以 emissive 超过 1 的部分直接被 sRGB 编码 clamp 成白。太阳因此是"纯白圆盘"而不是
  "带光晕的过曝太阳"。bloom 在 bgfx 后端不可用（见 `tina-postprocess-null-only`），
  所以本 ADR 不承诺光晕；
- 门禁：`tina_tests` 的 `NullRenderDeviceTextureTest` 覆盖默认值为 0、远大于 1 的值被接受、
  以及六种逐通道的非有限/负值被拒；`samples/3d_voxel --capture-luma` 的对照捕获须显示
  对准太阳的画面中心显著亮于同一仰角、反方向的天空。

  2026-09-02 实测（`--frames=150 --selftest-edits --capture-luma`）：对照帧钉住太阳相位、
  钉住偏航与俯仰，两帧只差朝向。中心 48×48：

  ```
  sun_disc_center=255,244,226   sun_away_center=91,119,161
  ```

  `sun_away` 正是该相位天空色的 sRGB 编码，即控制帧确实在看天空而非几何。
  `sun_disc_center` 与手算逐字节相符：emissive 线性 `(1.15, 0.8165, 0.667)` 加上
  白 albedo 吃到的 ambient 项 `0.09 + 0.04×0.09×0.25 = 0.0909`，编码后 `(255, 244, 226)`。
  也就是说 emissive 与 ambient 是**相加**而非相乘，正是本 ADR 要的解耦。

  两个反直觉的点，都是踩过才知道的：

  - **判据不能只看白色。** 首版用 `SunDiscRadiance = 9.0`，圆盘在整个白天都是纯白 —— 而纯白
    是许多无关缺陷共有的结果，证不出 emissive 通了。现在读黎明/中段相位，让一个通道饱和、
    两个通道存活，颜色就只能来自真正写进去的那三个数。
  - **暖色会被 clamp 悄悄吃掉。** `warmth` 算得再对，只要三个通道都大于 1 就全部编码成 255。
    `2.2` 时绿通道仅在 `warmth < 0.06` 才不饱和，等于整天都是白球。选值必须对着传输函数
    验算，不能只看线性域。
  - 48×48 patch 的均值（`255,247.90,228.94`）高于中心像素，因为背光球体在接近轮廓处
    `N·L` 由负转零，掠射角吃到直射阳光形成一圈边缘光。这是正确渲染，不是缺陷；
    判据因此取中心单像素而非均值。

## 被拒绝方案

- **产品侧用超过 1 的 `baseColorFactor` 撑亮 ambient 项。** 不动引擎就能做，实测也能变亮。
  拒绝理由是它把太阳亮度绑在 `ambientScale` 上：ambient 是场景照明参数，改它是为了调暗处细节，
  不该顺带改变太阳的亮度。这是一个没有出现在任何签名里的耦合。
- **emissive 放进 per-instance 的 instance data。** 好处是同一材质的不同实例能有不同自发光强度
  （粒子、闪烁的灯）。拒绝理由是它要改顶点布局与 batching，而当前没有需要它的用例；
  真需要时可以再加，那时 material factor 与 instance factor 相乘即可，不冲突。
- **给太阳单独一条不受光照的 pass。** 语义最干净，但要碰被冻结的 `RenderPassKind`，
  而 emissive 是一个更小、更通用、且 glTF 已有定义的解法。
- **把 emissive 塞进 `u_normalParams` 的 yzw 空位。** 省一个 uniform，但让 "normal params"
  这个名字变成谎话。uniform 数量不是当前的瓶颈。
