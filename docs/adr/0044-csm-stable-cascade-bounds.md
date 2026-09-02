# ADR 0044：级联阴影用外接球窗口 + texel 吸附换取时间稳定性

- 状态：Proposed
- 日期：2026-09-02
- 决策者：Tina maintainers

## 背景

`BgfxCascadedDirectionalShadowMath.cpp` 原先把每个级联的 light-space 窗口取成 frustum slice 八个角点的
紧 AABB，且全文没有任何 texel 吸附。这带来两个独立缺陷，都表现为"画面亮度随相机移动无理由地变化"：

1. **窗口尺寸随相机朝向变化。** 紧 AABB 的宽高取决于八角点在 light space 的投影，相机一转窗口就换一个
   大小，于是每个 texel 覆盖的世界尺寸每帧都不同。
2. **窗口位置随相机连续滑动。** `lightView` 的 eye/at 都锚在相机上，窗口中心跟着相机连续平移，shadow map
   每帧重新量化一次，所有阴影边缘持续抖动（shadow swimming）。

单独加 texel 吸附解决不了问题：栅格间距本身每帧在变，"吸附到整数个 texel"就没有跨帧含义。两者必须一起改。

## 决定

级联的横向（X/Y）窗口取 frustum slice 的**外接球**，边长 `2r`，窗口中心在**世界原点锚定**的 light space 中
吸附到整数个 atlas texel；窗口额外携带 1 个 texel 余量以容纳最多半 texel 的吸附偏移。深度轴（Z）既不吸附
也不用外接球，仍取八角点紧 AABB 加 `depthPaddingMeters`。

外接球半径 `r` 只由 split 深度与镜头（`tan(fov/2)`、aspect）决定，对相机位置与朝向不变，因此存在固定的
texel 栅格；`lightView` 锚在世界原点而非相机，"整数个 texel"才有跨帧含义（正交投影不关心 eye 沿光轴的位置，
锚点只影响 light-space 坐标的数值量级）。吸附栅格必须**就是** atlas 栅格，所以实际 tile 分辨率经
`BgfxCascadedDirectionalShadowInput::tileExtent` 传入数学层，而不是由数学层假定默认值。

## 结果

- 相机平移时级联窗口按整数 texel 跳变而非连续滑动，阴影边缘不再抖动；相机旋转不改变窗口尺寸。
- `BgfxCascadedDirectionalShadowCascade` 新增 `texelSizeMeters`，把"窗口每次跳变的量子"变成可断言的量。
- 成本：外接球比紧 AABB 宽，同一 tile 分辨率下有效阴影分辨率下降（等效于每级联覆盖更大世界范围）。这是
  稳定性换清晰度的标准取舍，不可两全。
- 深度轴刻意保持紧：Z 不做光栅化，caster 与 receiver 走同一变换，连续的深度平移在比较中相互抵消，所以 Z
  从外接球的旋转不变性得不到任何好处，反而会损失深度精度，并且会把远处沿光轴的几何拖进 depth pass —— 例如
  `samples/3d_voxel` 的 250m 太阳 billboard，它正是靠 Z 边界够紧才不投影。
- 门禁：`BgfxCascadedDirectionalShadowMathTests` 新增4项——窗口尺寸对相机位置/朝向不变、亚 texel 相机位移
  只让 bounds 跳整数个 texel、吸附后窗口仍完整包住 slice 八角点、texel 尺寸随配置的 tile extent 变化。
  原有7项保持通过。`samples/3d_voxel --capture-luma` 的 `shadow_brightened=0` 不变量继续成立。

## 被拒绝方案

- **只加 texel 吸附，保留紧 AABB 窗口。** 栅格间距每帧变化，吸附无跨帧含义，抖动依旧。
- **把 light view 继续锚在相机上再吸附。** 参考系本身随相机滑动，在滑动的坐标系里吸附等于没吸附。
- **横向也用紧 AABB、只对深度做外接球。** 方向恰好相反：需要旋转不变性的是被光栅化的横向轴。
- **提高 tile 分辨率来掩盖抖动。** 抖动是时间上的重新量化，不是空间分辨率不足；提分辨率只让抖动更细密，
  代价是显存与带宽。
