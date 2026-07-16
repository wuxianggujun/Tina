# GoogleTest 与验证

## 规则

- 测试框架固定为 GoogleTest；
- CMake 只生成 `tina_tests` 可执行文件，不注册额外测试调度；
- 构建完成后直接运行 `tina_tests`，返回码非0即失败；
- 测试依赖由固定 vcpkg baseline 提供；
- 测试日志不得包含路径外的敏感环境变量或凭据。

## 当前自动化覆盖（21项）

- Core：Result、ScopeExit、EnumFlags、Assert、Clock、FrameTimer、FixedStepTicker、基础类型和 Legacy Compatibility；
- Runtime 时间：固定步长、插值、禁用 Simulation、最大追赶步和异常步消费；
- Event：优先级队列、RAII Token、dispatcher 先销毁和立即取消订阅；
- Resource：共享 FileSystem 唯一 completion pump、主线程预算、取消和过期 generation 隔离。
- Windows 栈预算：EventSystem 实例不得重新引入超过默认线程栈预算的大块 inline queue；
- UI：hit-test 不隐式布局、重叠节点唯一命中、Capture/Target/Bubble 顺序和动态子节点上下文继承。

21 项测试已在 Windows 11、Visual Studio 2026 18.4.3、MSVC 19.50 和 Ubuntu 22.04/GCC 下直接运行通过；Clang ASan/UBSan 仍是独立门禁。

## 待补自动化门禁

- Application 初始化失败回滚和析构顺序；
- Scene 延迟 push/pop/replace；
- UI generation NodeId、Pointer Capture、Focus/Tab、节点销毁时安全失效；
- Render Pass 顺序、typed handle generation 和 NullRenderDevice 资源计数；
- 完整 Tina 游戏的 Linux Clang ASan/UBSan 构建与运行（当前测试程序已通过）。

Windows 和 Linux 必须分别构建；Linux Clang 额外运行 ASan/UBSan 配置。项目直接运行 GoogleTest 可执行文件，不使用 CTest 调度。

## 运行冒烟

菜单 2D + 中文 UI，正常提交300帧后退出：

```bash
./Tina --smoke-frames=300
```

直接进入完整 2D TileMap、ECS、Toolbar 和 CharacterPanel：

```bash
./Tina --smoke-game --smoke-frames=300
```

运行右手透视相机、深度测试和静态索引 Cube：

```bash
./Tina --smoke-3d --smoke-frames=300
```

三个命令都必须返回0，并在日志中出现正常初始化、达到帧数、场景退出、资源管理器释放、bgfx 和窗口关闭记录。3D 路径还必须出现 `Smoke3DScene released vertex and index buffers`，且不得出现 `BGFX LEAK` 或 `MEMORY LEAK`。

bgfx Debug/D3D11 当前会在关闭 InfoQueue 时输出一次 `RefCount is 4 (expected 0)`；同一代码的 MSVC Release 300 帧验证无该提示、无 stderr、无 leak marker，因此将其记录为第三方 Debug layer 诊断噪声，不作为 Tina 资源泄漏结论。
