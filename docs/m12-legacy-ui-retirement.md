# M12 Legacy 产品退役说明

> 用户已授权退役旧横版 2D 游戏与其 Legacy UI。产品源码和构建图删除已经完成。

## 已删除的产品范围

- Legacy `Tina.exe` 入口和单体产品 target；
- 旧 `game`、`engine`、ECS、renderer、particles、physics 产品模块；
- 旧 UI 产品实现与其专属测试/资源；
- CoreLegacy、Procedural 和 EASTL/EABase submodule；
- `src/vnext` 迁移前缀，当前模块已扁平为 `src/<module>`；
- Legacy 玩法音频、纹理、shader/config 等无引用资源；
- 根 `image/` 下的旧 2D Sandbox 产品截图与重复 mascot 副本。

历史上旧 UI 与当前 UI 曾使用过相同的 `src/ui` 路径名称。不能根据路径名判断退役状态：当前工作树的
`include/tina/ui` 与 `src/ui` 是 vNext Retained UI 产品实现，必须保留。

## 当前产品图

- 组合入口：`Tina::Desktop::CreateEngine`；
- Runtime：`EngineHost` + `IGameApplication` + `IGameState`；
- 产品样例：`tina_sample_2d`、`tina_sample_3d`；
- UI：`include/tina/ui` + `src/ui`；
- 真实 adapter：私有 GLFW、bgfx、可选 FreeType/miniaudio/Box2D。

`TINA_BUILD_LEGACY` 固定为 OFF；显式 ON 必须 `FATAL_ERROR`。该选项只保留为明确拒绝旧构建请求的
迁移护栏，不代表 Legacy 仍可构建。

## 有意保留

| 项 | 理由 |
| --- | --- |
| `thirdparty/bgfx.cmake` | 当前唯一真实 Render backend 的锁定源码依赖 |
| `branding/Tina.jpg` | 引擎品牌资产，不是 Legacy 玩法资源 |
| `resources/fonts/SourceHanSansSC-Regular.otf` | 可选 FreeType/CJK fixture；可用 `TINA_UI_FONT_PATH` 外置 |

## 清理收口

`CLEAN-001`～`CLEAN-004` 已完成。2026-08-19 的定向资源复核继续删除了无消费者的根 `image/`
目录，并移除了根 CMake 中不再参与任何资源复制或 staging 的 `BUILD_DIR`、`RESOURCE_DIR` 与
`RESOURCE_DEST_DIR` 缓存变量。

当前仓库资源均有明确边界：`branding/Tina.jpg` 是唯一品牌图；
`resources/fonts/SourceHanSansSC-Regular.otf` 是可选 FreeType/CJK fixture；`tests/fixtures/gltf/` 由
Asset 测试与 3D 产品 sample 消费。vNext sample 继续自行 staging cooked asset，根 CMake 不扫描或复制
源码 `resources/`。

## 复验

禁止 clean-first：

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

产品退役证据只证明旧产品不再参与构建/运行，不替代当前 2D、3D、UI、Audio 和 Linux 门禁。
