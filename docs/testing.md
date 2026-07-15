# GoogleTest 与验证

## 规则

- 测试框架固定为 GoogleTest；
- CMake 只生成 `tina_tests` 可执行文件，不注册额外测试调度；
- 构建完成后直接运行 `tina_tests`，返回码非0即失败；
- 测试依赖由固定 vcpkg baseline 提供；
- 测试日志不得包含路径外的敏感环境变量或凭据。

## 覆盖范围

- Core：Result、ScopeExit、EnumFlags、Assert、Clock、FrameTimer；
- Runtime：固定步长、最大追赶步、阶段顺序、失败回滚；
- Event：订阅析构、队列顺序、dispatcher 先销毁；
- Resource：状态转换、取消、过期 generation、唯一 completion pump；
- Scene/UI：延迟 push/pop、布局 dirty、hit-test、Capture/Target/Bubble；
- Render：Pass 顺序、typed handle generation、资源释放计数。

Windows 和 Linux 必须分别构建；Linux Clang 额外运行 ASan/UBSan 配置。

## 运行冒烟

菜单 2D + 中文 UI，正常提交120帧后退出：

```bash
./Tina --smoke-frames=120
```

直接进入完整 2D TileMap、ECS、Toolbar 和 CharacterPanel：

```bash
./Tina --smoke-game --smoke-frames=120
```

两个命令都必须返回0，并在日志中出现正常初始化、达到帧数、场景退出、资源管理器释放、bgfx 和窗口关闭记录。
