# ADR 0009：Runtime 只读 Cooked Asset，cgltf 只在 Cooker

- 状态：Accepted
- 日期：2026-07-16

## 背景

Runtime 直接解析 glTF、图片、字体或 shader 源文件，会把解析依赖、错误分支和不确定 IO 带入
每台玩家机器，也难以建立稳定 schema、增量构建和性能预算。

## 决定

`tina_assetc` 将源资产转换为带 schema version、依赖、内容 Hash 和类型信息的 Cooked Asset；
Runtime 只读取 Cooked 格式。glTF 首期解析固定使用 cgltf v1.15 单文件版本，并只存在于
Cooker adapter。首期支持静态三角形 Mesh、节点层级、Position/Normal/UV0/Index、基础颜色和
纹理；Skin、Animation、Morph 与不支持的压缩扩展必须返回明确诊断，不能静默降级。
稳定 AssetId 由显式 import 一次分配并保存在 metadata/catalog，不由路径或内容 Hash 推导；
普通 cook 缺少或发现重复 ID 时失败。
源依赖必须 canonicalize 后仍位于允许 root；拒绝绝对/远程/路径逃逸 URI，并在任何分配前
验证 count/offset/stride/decoded size 上限。

## 结果

- Cooker/Runtime 通过独立 `tina_asset_format` 共享版本化格式；
- 产物与 Manifest 先验证再事务提交，失败保留上一个可用版本；
- Runtime 发布包不包含 cgltf 或源资产解析路径；
- Bundle/Patch 和在线热重载后置，不污染首期格式。

## 被拒绝方案

- Runtime 直接加载 glTF：启动成本、诊断和依赖不可控；
- 复制 Carbon 的 cgltf 副本：版本与许可证来源不清晰，应从上游锁定。
