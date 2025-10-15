# Tina - 2D沙盒游戏引擎

**[English](README.md) | 简体中文**

[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/c%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B#Standardization)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT)

一个使用现代C++17开发的2D沙盒游戏引擎，采用bgfx渲染后端，支持地形编辑、流体模拟、ECS架构等特性。

![Tina](./image/Tina.jpg)

## 🎮 游戏截图

### 主菜单
![主菜单](./image/MenuScene.png)

### 世界选择
![世界选择](./image/WorldListView.png)

### 游戏场景
![游戏场景](./image/GameSceneView.png)

## ✨ 核心特性

### 🎨 渲染系统
- **基于bgfx的跨平台渲染** - 支持DX11/OpenGL/Metal/Vulkan后端
- **多视图渲染架构** - 分层渲染（固体层、半透明层、UI层）
- **2D瓦片地图渲染** - 高效的批量渲染
- **粒子系统** - 支持爆炸、碎片等视觉效果
- **昼夜循环系统** - 动态光照和天空颜色变化

### 🎮 游戏玩法
- **地形编辑** - 实时挖掘、填充、爆破
- **流体模拟** - 先进的水流模拟系统（CA算法）
- **物理系统** - AABB碰撞检测、重力模拟
- **角色控制** - 平台跳跃、多角色切换
- **程序生成** - 随机地形生成（Perlin噪声）

### 🏗️ 引擎架构
- **ECS架构** - 基于EnTT的实体组件系统
- **场景管理** - 支持场景栈（push/pop/replace）
- **事件系统** - 双层事件总线（OS层 + 玩法层）
- **资源管理** - 异步资源加载、智能指针管理
- **UI系统** - 自定义UI框架，支持布局、事件、动画

### 🖼️ UI系统实现

#### 已实现的UI组件
- **UINode** - UI节点基类（变换、层级、可见性）
- **UIButton** - 按钮组件（悬停、点击、禁用状态）
- **UIPanel** - 面板容器（背景、边框、圆角）
- **UIToolbar** - 工具栏（多槽位、图标、选择状态）
- **UIDialog** - 对话框系统（居中、遮罩）
- **UICharacterPanel** - 角色信息面板（血条、名称、控制按钮）
- **TextRenderer** - 文本渲染器（TrueType字体、对齐、颜色）

#### UI功能特性
- **自动布局管理** - 响应式窗口大小调整
- **事件处理** - 鼠标悬停、点击、滚轮
- **RAII渲染作用域** - 自动管理渲染状态
- **颜色主题** - 预定义颜色常量
- **DPI缩放** - 高分辨率显示支持

### 🎬 场景系统

#### 已实现的场景
1. **MenuScene** - 主菜单场景
   - 开始游戏、设置、退出按钮
   - 背景渲染、标题显示
   
2. **WorldSelectScene** - 世界选择场景
   - 世界列表（3个预设世界）
   - 创建新世界对话框
   - 种子输入、世界名称编辑
   
3. **GameScene** - 游戏主场景
   - 地形渲染（固体/水流）
   - 角色系统（玩家+3个NPC）
   - 工具栏（8个工具槽位）
   - 角色信息面板
   - 昼夜循环
   
4. **PauseScene** - 暂停菜单
   - 继续游戏、设置、返回主菜单
   - 半透明遮罩
   
5. **SettingsScene** - 设置场景
   - 音效/音乐音量调节
   - 全屏切换
   - 昼夜时间调整

### 🎵 音频系统
- **基于SDL_mixer** - 支持MP3/WAV/OGG
- **音效管理** - 淡入淡出、音量控制
- **分组管理** - 音效/音乐独立控制
- **异步加载** - 资源懒加载

### 🎯 输入系统
- **InputSystem** - 统一的输入管理
- **键盘输入** - 键盘状态查询、按键事件
- **鼠标输入** - 坐标转换、按钮状态、滚轮
- **窗口事件** - 窗口调整、最大化、关闭

### 📦 项目结构

```
Tina/
├── src/
│   ├── core/           # 核心系统（日志、时间、内存）
│   ├── engine/         # 引擎核心（应用、场景、事件、输入）
│   ├── renderer/       # 渲染系统（着色器、纹理、瓦片渲染）
│   ├── ui/             # UI系统（组件、渲染器、布局）
│   ├── ecs/            # ECS系统（组件、系统）
│   └── game/           # 游戏逻辑（场景、地图、角色）
├── resources/          # 资源文件（着色器、纹理、音频、字体）
├── image/              # 游戏截图
└── docs/               # 文档
```

### 🎮 操作指南

**主菜单**
- 点击"开始游戏"进入世界选择
- 点击"设置"调整音量和显示选项
- 点击"退出"关闭游戏

**游戏场景**
- `A/D` 或 `←/→` - 移动角色
- `W/空格` - 跳跃
- `左键` - 使用当前工具（挖掘/放置/爆破）
- `右键` - 查看角色信息并切换控制
- `滚轮/数字键` - 切换工具
- `ESC` - 暂停菜单

**工具说明**
- **工具0** - 注水器（在点击位置放置水）
- **工具1** - 挖掘器（圆形范围挖掘）
- **工具2** - 爆炸器（大范围破坏+粒子效果）

## 📋 快速开始

### 克隆仓库

```bash
git clone https://github.com/wuxianggujun/Tina.git
cd Tina
git submodule update --init --recursive
```

### Windows 编译

**前置要求：**
- CMake 3.20+
- Visual Studio 2019/2022
- Windows SDK

**编译步骤：**
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Ubuntu 编译

**安装依赖：**
```shell
sudo add-apt-repository ppa:git-core/ppa
sudo apt update
sudo apt install git
sudo apt install ninja-build
```

```shell
sudo snap install cmake --classic
sudo ln -s /snap/cmake/current/bin/cmake /usr/bin/cmake
sudo ln -s /snap/cmake/current/bin/ccmake /usr/bin/ccmake
sudo ln -s /snap/cmake/current/bin/cpack /usr/bin/cpack
```

```shell
# 安装图形库依赖
sudo apt install libgl1-mesa-dev libglfw3-dev
sudo apt install libwayland-dev libwayland-egl-backend-dev libxkbcommon-dev xorg-dev 
sudo apt install libx11-dev libxext-dev libxtst-dev libxrender-dev libxmu-dev libxmuu-dev
sudo apt install pkg-config
```

**编译步骤：**
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./Tina
```

## 🔧 技术栈

### 核心依赖

| 库 | 版本 | 用途 |
|---|---|---|
| [bgfx](https://github.com/bkaradzic/bgfx.cmake.git) | latest | 跨平台渲染库（支持DX11/OpenGL/Vulkan） |
| [SDL2](https://www.libsdl.org/) | 2.0+ | 窗口管理、输入处理 |
| [SDL_mixer](https://www.libsdl.org/projects/SDL_mixer/) | 2.0+ | 音频播放（MP3/WAV/OGG） |
| [EnTT](https://github.com/skypjack/entt.git) | 3.x | 实体组件系统（ECS） |
| [spdlog](https://github.com/gabime/spdlog.git) | 1.x | 高性能日志库 |
| [stb](https://github.com/wuxianggujun/stb-cmake.git) | latest | 图像加载、TrueType字体 |
| [GLM](https://github.com/g-truc/glm.git) | 0.9.9+ | 数学库（向量、矩阵） |

### 开发工具

- **[Google Test](https://github.com/google/googletest.git)** - 单元测试框架
- **[Tracy](https://github.com/wolfpld/tracy.git)** - 性能分析工具

## 📚 学习资源

本项目开发过程中参考了以下优秀的开源项目：

### 游戏引擎
- [MitchEngine](https://github.com/wobbier/MitchEngine) - 现代C++游戏引擎
- [CatDogEngine](https://github.com/CatDogEngine/CatDogEngine) - 数据驱动的游戏引擎
- [EraEngine](https://github.com/EldarMuradov/EraEngine) - Vulkan/DX12渲染引擎
- [ElvenEngine](https://github.com/denyskryvytskyi/ElvenEngine) - 2D游戏引擎
- [simple_engine](https://github.com/bikemurt/simple_engine) - 简洁的2D引擎

### 沙盒/平台游戏
- [Platformer](https://github.com/Somgonk/Platformer) - 2D平台跳跃游戏
- [OpenMiner](https://github.com/Unarelith/OpenMiner) - Minecraft风格沙盒
- [PainterEngine](https://github.com/matrixcascade/PainterEngine) - 2D图形引擎

### 架构设计
- [2019-ecs](https://code.austinmorlan.com/austin/2019-ecs) - ECS架构教程
- [Spatial.Engine](https://github.com/luizgabriel/Spatial.Engine) - 空间引擎

### bgfx相关
- [efkbgfx](https://github.com/cloudwu/efkbgfx) - Effekseer粒子系统+bgfx
- [ant](https://github.com/ejoy/ant) - Lua游戏引擎（使用bgfx）

### 工具库
- [spdlog_wrapper](https://github.com/gqw/spdlog_wrapper) - spdlog封装
- [klog](https://github.com/KkemChen/klog) - 轻量级日志库

## 🎯 事件系统详解

引擎采用**双层事件总线架构**，分离OS事件和游戏逻辑事件：

### 第一层：OS事件总线（OSEventBus）

处理操作系统级别的事件：
- **键盘事件** - 按键按下/释放
- **鼠标事件** - 移动、点击、滚轮
- **窗口事件** - 调整大小、最大化、关闭

**使用示例：**
```cpp
// 订阅键盘按下事件
auto connection = app()->osEvents().onKeyPressed.connect(this, &MyScene::onKeyPressed);

void MyScene::onKeyPressed(const KeyEvent& e) {
    if (e.key == KeyCode::Escape) {
        // 打开暂停菜单
    }
}
```

### 第二层：强类型事件总线（TypedEventBus）

基于`entt::dispatcher`，用于游戏逻辑事件：
- **类型安全** - 编译期检查
- **无需修改引擎** - 扩展性强
- **RAII自动管理** - 自动取消订阅

**使用示例：**
```cpp
// 1. 定义事件类型
namespace Events {
    struct PlayerJumped { float height; };
    struct SetDayNight { float normalized; };
}

// 2. 订阅事件（使用SubscriptionManager自动管理生命周期）
TINA_SUBSCRIBE_EVENT(m_subscriptions, Events::PlayerJumped, MyScene::onPlayerJumped);

// 3. 触发事件
Events::PlayerJumped event{2.5f};
app()->events().trigger(event);

// 4. 处理事件
void MyScene::onPlayerJumped(const Events::PlayerJumped& e) {
    TINA_INFO("玩家跳跃高度: {}", e.height);
}
```

**优势：**
- ✅ 解耦模块间通信
- ✅ 支持异步事件处理
- ✅ 自动管理订阅生命周期
- ✅ 类型安全，编译期检查

详细说明请见 `docs/event_system.md`

## 📝 项目状态

**当前版本：** 1.0.0  
**开发状态：** 🔒 已归档（不再维护）

本项目作为学习项目已完成预定目标，实现了：
- ✅ 完整的2D沙盒游戏框架
- ✅ 现代C++17架构实践
- ✅ 跨平台渲染系统
- ✅ ECS架构实现
- ✅ 流体模拟算法
- ✅ 完整的UI系统

项目代码可供学习和参考使用。如果你想基于此项目继续开发，欢迎Fork！

## 🐛 已知问题

- **性能优化** - 大地图渲染帧率可能下降
- **流体模拟** - 极端情况下可能出现不稳定
- **UI缩放** - 部分UI在高DPI下可能显示异常
- **音频** - 偶尔会有音频卡顿

## 🎓 学习要点

如果你想学习这个项目，建议关注以下模块：

1. **场景管理系统** (`src/engine/SceneManager.cpp`)
   - 场景栈实现
   - 生命周期管理
   - 视图配置

2. **ECS架构** (`src/ecs/`)
   - 组件定义
   - 系统实现
   - 实体管理

3. **事件系统** (`src/engine/EventSystem.cpp`)
   - 双层事件总线
   - RAII订阅管理
   - 强类型事件

4. **UI框架** (`src/ui/`)
   - 自定义UI组件
   - 布局系统
   - 事件处理

5. **流体模拟** (`src/game/TileMap.cpp`)
   - 元胞自动机算法
   - 水流压力计算
   - 优化技巧

## 🤝 致谢

感谢以下组织和项目：

- **JetBrains** - 提供开源项目免费许可证

  <a href="https://jb.gg/OpenSourceSupport"><img src="./img/jb_beam.svg" width="200"/></a>

- **所有贡献者** - 感谢所有提供反馈和建议的开发者
- **开源社区** - 感谢所有被参考的优秀开源项目

## 📄 许可证

本项目采用 [MIT License](LICENSE) 开源许可证。

```
MIT License

Copyright (c) 2024 wuxianggujun

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

**⭐ 如果这个项目对你有帮助，欢迎给个Star！**

**📧 联系方式：** [GitHub Issues](https://github.com/wuxianggujun/Tina/issues)
