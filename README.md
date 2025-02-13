# Tina Engine

Tina 是一个模块化的游戏引擎，采用现代 C++ 开发，使用 CMake 构建系统。引擎设计注重模块化、可扩展性和易用性。

## 项目结构

```
Tina/
├── engine/                    # 引擎核心代码
│   ├── core/                  # 核心功能模块
│   │   ├── include/          
│   │   │   └── tina/         # 公共头文件
│   │   │       ├── core/     # 核心功能
│   │   │       └── render/   # 渲染功能
│   │   ├── src/             
│   │   │   └── render/      # 渲染实现
│   │   └── CMakeLists.txt   
│   └── CMakeLists.txt       
│
├── third_party/              # 第三方依赖
│   ├── bgfx.cmake/          # BGFX 图形库
│   └── googletest/          # Google Test 测试框架
│
├── examples/                 # 示例代码
│   ├── basic_usage/         # 基础使用示例
│   └── CMakeLists.txt
│
├── tests/                    # 单元测试
│   ├── core/                # 核心模块测试
│   └── CMakeLists.txt
│
└── CMakeLists.txt           # 主构建文件

```

## 核心特性

- **模块化设计**: 引擎采用模块化架构，便于扩展和维护
- **现代渲染**: 使用 BGFX 图形库，支持多种渲染后端
- **跨平台**: 支持 Windows、Linux、MacOS 等主流平台
- **单元测试**: 使用 Google Test 框架确保代码质量

## 命名空间结构

- `Tina::Core`: 核心功能模块
- `Tina::Render`: 渲染系统
- `Tina::Network`: 网络功能（计划中）
- `Tina::Utils`: 工具类（计划中）

## 构建系统

项目使用 CMake 构建系统，支持以下选项：

```bash
# 配置选项
TINA_BUILD_SHARED_LIBS  # 构建为动态库（默认：OFF）
TINA_BUILD_EXAMPLES     # 构建示例（默认：ON）
TINA_BUILD_TESTS       # 构建测试（默认：ON）
```

### 构建步骤

```bash
# 克隆renderer/refactor分支
git clone -b renderer/refactor https://github.com/wuxianggujun/Tina.git 

# 进入项目目录
cd Tina 

# 验证克隆分支（可选）
git branch 

# 应显示* renderer/refactor
# 配置项目
cmake -B build

# 构建项目
cmake --build build

# 运行测试（如果启用）
cd build && ctest
```

## 开发规范

1. 代码风格
   - 使用现代 C++ 特性 (C++17)
   - 遵循命名空间规范
   - 使用 UTF-8 编码

2. 模块开发
   - 每个模块都应该有清晰的职责
   - 提供完整的单元测试
   - 保持良好的文档注释

3. 依赖管理
   - 第三方库统一放置在 `third_party` 目录
   - 使用 git submodule 管理外部依赖

## 示例代码

```cpp
#include "tina/core/Core.hpp"
#include "tina/render/Renderer.hpp"

int main() {
    Tina::Core::Engine engine;
    Tina::Render::Renderer renderer;

    engine.initialize();
    renderer.initialize(1280, 720, "Tina Engine");

    while (true) {
        renderer.beginFrame();
        // 渲染代码
        renderer.endFrame();
    }

    renderer.shutdown();
    engine.shutdown();
    return 0;
}
```

## 许可证

[待定]

## 贡献指南

[待定]
varying.def.sc 不能添加任何注释，不然Shader无法通过编译！