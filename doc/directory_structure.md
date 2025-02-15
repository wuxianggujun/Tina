# Tina引擎目录结构

## 项目结构概览

```
engine/
├── CMakeLists.txt
├── core/
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── tina/
│   │       ├── window/          # 窗口管理系统
│   │       │   ├── GlfwMemoryManager.hpp
│   │       │   ├── WindowManager.hpp
│   │       │   └── Window.hpp
│   │       ├── components/      # 组件系统
│   │       │   ├── SpriteComponent.hpp
│   │       │   ├── ComponentDependencyManager.hpp
│   │       │   ├── Renderable2DComponent.hpp
│   │       │   ├── RectangleComponent.hpp
│   │       │   ├── RenderableComponent.hpp
│   │       │   └── Transform2DComponent.hpp
│   │       ├── renderer/        # 渲染系统
│   │       │   ├── Scene2DRenderer.hpp
│   │       │   ├── BatchRenderer2D.hpp
│   │       │   ├── ShaderManager.hpp
│   │       │   ├── TextureManager.hpp
│   │       │   ├── Texture2D.hpp
│   │       │   ├── TextureAtlas.hpp
│   │       │   ├── RenderCommand.hpp
│   │       │   ├── Renderer2D.hpp
│   │       │   ├── RenderSystem.hpp
│   │       │   ├── IRenderSystem.hpp
│   │       │   ├── Color.hpp
│   │       │   └── Renderer.hpp
│   │       ├── core/           # 核心系统
│   │       │   ├── Engine.hpp
│   │       │   ├── OrthographicCamera.hpp
│   │       │   ├── Timer.hpp
│   │       │   ├── Context.hpp
│   │       │   ├── Core.hpp
│   │       │   └── filesystem.hpp
│   │       ├── layer/          # 层级系统
│   │       │   ├── Layer.hpp
│   │       │   ├── GameLayer.hpp
│   │       │   ├── Render2DLayer.hpp
│   │       │   └── LayerStack.hpp
│   │       ├── utils/          # 工具类
│   │       │   ├── stb_image.h
│   │       │   ├── Profiler.hpp
│   │       │   ├── ComponentPool.hpp
│   │       │   ├── ObjectPool.hpp
│   │       │   └── BgfxUtils.hpp
│   │       ├── resources/      # 资源管理
│   │       │   ├── ResourceManager.hpp
│   │       │   ├── ResourcePackage.hpp
│   │       │   ├── AsyncResourceLoader.hpp
│   │       │   ├── TextureLoader.hpp
│   │       │   └── FileWatcher.hpp
│   │       ├── scene/          # 场景系统
│   │       │   ├── Scene.hpp
│   │       │   ├── SceneManager.hpp
│   │       │   └── Components.hpp
│   │       ├── math/           # 数学库
│   │       ├── log/            # 日志系统
│   │       │   └── Logger.hpp
│   │       ├── event/          # 事件系统
│   │       │   ├── EventQueue.hpp
│   │       │   └── Event.hpp
│   │       └── container/      # 容器类
│   └── src/
│       ├── window/            # 窗口管理实现
│       │   ├── GlfwMemoryManager.cpp
│       │   ├── Window.cpp
│       │   └── WindowManager.cpp
│       ├── scene/             # 场景系统实现
│       │   ├── Scene.cpp
│       │   └── SceneManager.cpp
│       ├── utils/             # 工具类实现
│       │   └── BgfxUtils.cpp
│       ├── renderer/          # 渲染系统实现
│       │   ├── Scene2DRenderer.cpp
│       │   ├── BatchRenderer2D.cpp
│       │   ├── TextureManager.cpp
│       │   ├── ShaderManager.cpp
│       │   ├── Texture2D.cpp
│       │   ├── Renderer2D.cpp
│       │   ├── RenderCommand.cpp
│       │   ├── TextureAtlas.cpp
│       │   ├── RenderSystem.cpp
│       │   └── Renderer.cpp
│       ├── core/              # 核心系统实现
│       │   ├── Engine.cpp
│       │   ├── Timer.cpp
│       │   └── Context.cpp
│       ├── layer/             # 层级系统实现
│       │   ├── Render2DLayer.cpp
│       │   ├── Layer.cpp
│       │   ├── GameLayer.cpp
│       │   └── LayerStack.cpp
│       ├── resources/         # 资源管理实现
│       │   ├── TextureLoader.cpp
│       │   └── ResourceManager.cpp
│       ├── components/        # 组件系统实现
│       │   ├── SpriteComponent.cpp
│       │   └── RectangleComponent.cpp
│       ├── log/               # 日志系统实现
│       │   └── Logger.cpp
│       ├── event/             # 事件系统实现
│       │   └── EventQueue.cpp
│       └── container/         # 容器类实现
├── resources/                 # 引擎资源文件
├── utils/                     # 通用工具库
└── network/                   # 网络功能模块

```

## 文件说明

### 窗口系统 (window/)
- **GlfwMemoryManager.hpp/cpp**: GLFW内存管理器实现
- **WindowManager.hpp/cpp**: 窗口管理器实现
- **Window.hpp/cpp**: 窗口类实现

### 渲染系统 (renderer/)
- **Scene2DRenderer.hpp/cpp**: 2D场景渲染器
- **BatchRenderer2D.hpp/cpp**: 2D批量渲染器
- **ShaderManager.hpp/cpp**: 着色器管理器
- **TextureManager.hpp/cpp**: 纹理管理器
- **Texture2D.hpp/cpp**: 2D纹理实现
- **TextureAtlas.hpp/cpp**: 纹理图集实现
- **RenderCommand.hpp/cpp**: 渲染命令
- **Renderer2D.hpp/cpp**: 2D渲染器
- **RenderSystem.hpp/cpp**: 渲染系统
- **IRenderSystem.hpp**: 渲染系统接口
- **Color.hpp**: 颜色类
- **Renderer.hpp/cpp**: 基础渲染器

### 核心系统 (core/)
- **Engine.hpp/cpp**: 引擎核心实现
- **OrthographicCamera.hpp**: 正交相机实现
- **Timer.hpp/cpp**: 计时器实现
- **Context.hpp/cpp**: 上下文管理
- **Core.hpp**: 核心定义和宏
- **filesystem.hpp**: 文件系统实现

### 层级系统 (layer/)
- **Layer.hpp/cpp**: 基础层级类
- **GameLayer.hpp/cpp**: 游戏层实现
- **Render2DLayer.hpp/cpp**: 2D渲染层
- **LayerStack.hpp/cpp**: 层级栈管理

### 组件系统 (components/)
- **SpriteComponent.hpp/cpp**: 精灵组件
- **ComponentDependencyManager.hpp**: 组件依赖管理
- **Renderable2DComponent.hpp**: 2D渲染组件基类
- **RectangleComponent.hpp/cpp**: 矩形组件
- **RenderableComponent.hpp**: 可渲染组件基类
- **Transform2DComponent.hpp**: 2D变换组件

### 日志系统 (log/)
- **Logger.hpp/cpp**: 日志系统实现

### 事件系统 (event/)
- **EventQueue.hpp/cpp**: 事件队列实现
- **Event.hpp**: 事件类型定义

### 场景系统 (scene/)
- **Scene.hpp/cpp**: 场景类实现
- **SceneManager.hpp/cpp**: 场景管理器
- **Components.hpp**: 组件类型定义

### 工具类 (utils/)
- **stb_image.h**: 图片加载库
- **Profiler.hpp**: 性能分析工具
- **ComponentPool.hpp**: 组件对象池
- **ObjectPool.hpp**: 通用对象池
- **BgfxUtils.hpp/cpp**: BGFX工具函数

### 资源管理 (resources/)
- **ResourceManager.hpp/cpp**: 资源管理器
- **ResourcePackage.hpp**: 资源包管理
- **AsyncResourceLoader.hpp**: 异步资源加载器
- **TextureLoader.hpp/cpp**: 纹理加载器
- **FileWatcher.hpp**: 文件监视器

## 模块说明

### core/ - 核心模块
- 包含引擎的核心功能实现
- 定义了基础的类和接口
- 管理引擎的生命周期

### include/tina/ - 头文件
- window/: 窗口管理相关的接口定义
- components/: 组件系统的接口定义
- renderer/: 渲染系统的接口定义
- core/: 核心系统的接口定义
- layer/: 层级系统的接口定义
- utils/: 工具类的接口定义
- resources/: 资源管理的接口定义
- scene/: 场景系统的接口定义
- math/: 数学库的接口定义
- log/: 日志系统的接口定义
- event/: 事件系统的接口定义
- container/: 容器类的接口定义

### src/ - 源文件
- window/: 窗口管理的具体实现
- scene/: 场景系统的具体实现
- utils/: 工具类的具体实现
- renderer/: 渲染系统的具体实现
- core/: 核心系统的具体实现
- layer/: 层级系统的具体实现
- resources/: 资源管理的具体实现
- components/: 组件系统的具体实现
- log/: 日志系统的具体实现
- event/: 事件系统的具体实现
- container/: 容器类的具体实现

### resources/ - 资源文件
- 包含引擎运行所需的资源
- 着色器文件
- 纹理资源
- 配置文件

### utils/ - 工具库
- 通用工具函数
- 辅助类
- 工具组件

### network/ - 网络模块
- 网络通信功能
- 多人游戏支持
- 网络同步 