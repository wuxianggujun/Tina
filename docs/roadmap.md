# Roadmap

## M0 基线保护（已完成）

- 删除旧 worktree；
- 以当前 GLFW/miniaudio/vcpkg/Core 修改为主建立独立分支；
- 创建不可丢失的基线提交。

## M1 构建与生命周期（已完成首轮）

- GoogleTest 独立测试程序迁移（已完成，直接运行 `tina_tests`）；
- 修复 Application 初始化、Frame Phase、Event/Resource pump 和 shutdown；
- 通过 2D/3D 退出冒烟验证资源释放；自动化资源计数仍在后续测试门禁中。

## M2 2D + 自研 UI（已完成首轮）

- 统一每个 Scene 的 UI 树所有权，并由 EventSystem 对当前 roots 做单次输入路由；
- 修复布局注册、dirty 传播和单次输入路由；
- 运行主菜单或独立 2D/UI 冒烟程序。

## M3 最小 3D（已完成）

- 增加 Perspective Camera、深度缓冲和静态 Mesh；
- 建立独立 `--smoke-3d` 运行入口，不影响现有 2D 游戏；
- 验证退出时 GPU 资源全部释放。

## Later

Cooked Asset、Box2D/PhysX 模块、PBR、阴影、动画、脚本、编辑器和复杂文字输入。
