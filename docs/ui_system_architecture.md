# Tina UI 系统架构文档

> **版本**: 2.0
> **最后更新**: 2025-10-10（深夜）
> **适用项目**: Tina 2D 沙盒游戏引擎
> **重大更新**: UICore 已重构为 RenderQueue + UIRenderer 架构

---

## 目录

1. [系统概述](#系统概述)
2. [架构设计](#架构设计)
3. [核心类详解](#核心类详解)
4. [渲染系统](#渲染系统)
5. [事件系统](#事件系统)
6. [使用示例](#使用示例)
7. [性能优化](#性能优化)
8. [扩展指南](#扩展指南)

---

## 系统概述

### 设计目标

Tina UI 系统是一个**轻量级、树形结构的即时模式 UI 框架**，专为 2D 游戏 HUD 和调试界面设计。

**核心特性**：
- ✅ 树形层级结构（组合模式）
- ✅ 相对坐标 + 9 种锚点对齐
- ✅ 自动事件分发（hover、click）
- ✅ FreeType 文本渲染（UTF-8、中英文混排）
- ✅ bgfx 集成（跨平台图形后端）
- ✅ 即时模式渲染（每帧重建几何）

### 技术栈

| 组件 | 技术 | 说明 |
|------|------|------|
| **图形后端** | bgfx | 跨平台抽象层（DX11/OpenGL/Vulkan/Metal） |
| **字体渲染** | FreeType 2 | 矢量字体栅格化 |
| **窗口系统** | SDL3 | 跨平台窗口/输入管理 |
| **容器** | EASTL | 高性能 STL 替代品 |
| **数学库** | GLM | 向量/矩阵运算 |

---

## 架构设计

### 四层架构

```
┌─────────────────────────────────────────────────┐
│  应用层 (main.cpp)                               │
│  - 创建 UI 树                                    │
│  - 绑定业务逻辑回调                              │
│  - 主循环驱动                                    │
└──────────────────┬──────────────────────────────┘
                   ↓
┌─────────────────────────────────────────────────┐
│  组件层 (UIComponents)                           │
│  - UIPanel: 纯色矩形面板                         │
│  - UIButton: 可点击按钮                          │
│  - UILabel: 文本标签                             │
└──────────────────┬──────────────────────────────┘
                   ↓
┌─────────────────────────────────────────────────┐
│  UI 树层 (UINode)                                │
│  - 层级管理 (addChild/removeChild)               │
│  - 坐标变换 (local → world)                      │
│  - 递归渲染/更新                                 │
│  - 事件接口 (onClick/onHover)                    │
└──────────────────┬──────────────────────────────┘
                   ↓
┌─────────────────────────────────────────────────┐
│  渲染层 (UIRenderer + RenderQueue + TextRenderer)│
│  - UIRenderer: 收集渲染命令，提交到 RenderQueue  │
│  - RenderQueue: 批处理优化（矩形/精灵）          │
│  - TextRenderer: 专职文本渲染（FreeType 图集）   │
│  - SceneRenderer: 场景级渲染（渐变背景等）       │
└──────────────────┬──────────────────────────────┘
                   ↓
               bgfx → GPU
```

### 目录结构

```
src/ui/
├── UINode.hpp/cpp          # UI 树节点基类
├── UIComponents.hpp/cpp    # 具体 UI 组件
├── UIEventSystem.hpp/cpp   # 事件系统
├── UIRenderer.hpp/cpp      # UI 渲染器（使用 RenderQueue）
└── TextRenderer.hpp/cpp    # 专职文本渲染器

src/renderer/
├── RenderQueue.hpp/cpp     # 批处理渲染队列
├── RenderCommand.hpp       # 渲染命令定义
└── ShaderManager.hpp/cpp   # 着色器管理

src/engine/
├── Scene.hpp/cpp           # 场景基类（集成渲染组件）
└── SceneRenderer.hpp/cpp   # 场景级渲染（渐变等）
```

---

## 核心类详解

### UINode（UI 树节点基类）

**文件**: `src/ui/UINode.hpp`, `src/ui/UINode.cpp`

#### 类职责

UINode 是所有 UI 元素的**抽象基类**，提供：
1. **层级管理**：父子节点关系
2. **坐标变换**：局部坐标 → 世界坐标
3. **递归渲染**：自动遍历子树
4. **事件接口**：点击、hover 回调

#### 核心方法

##### 1. `addChild(UINode* child)` - 添加子节点

```cpp
void UINode::addChild(UINode* child)
{
    if (!child || child == this) return;  // 防御：空指针、自引用

    // 如果子节点已有父节点，先移除
    if (child->m_parent) {
        child->m_parent->removeChild(child);
    }

    // 建立父子关系
    child->m_parent = this;
    m_children.push_back(child);

    // 标记坐标脏（需重新计算世界坐标）
    child->m_dirty = true;
}
```

**使用场景**：
```cpp
UIPanel* panel = new UIPanel("DebugPanel");
UIButton* btn = new UIButton("ClearBtn");
panel->addChild(btn);  // btn 的坐标相对于 panel
```

**注意事项**：
- ✅ 自动处理重复添加（先移除旧父节点）
- ✅ 防止自引用（`child == this`）
- ⚠️ 调用者负责内存管理（析构时自动 delete 子节点）

---

##### 2. `getWorldPosition()` - 获取世界坐标

```cpp
Tina::Math::Vec2 UINode::getWorldPosition()
{
    updateWorldTransform();  // 延迟计算（脏标记优化）
    return m_worldPos;
}
```

```cpp
void UINode::updateWorldTransform()
{
    if (!m_dirty) return;  // 跳过干净节点

    if (m_parent) {
        // 递归计算：父节点世界坐标 + 锚点偏移 + 局部偏移
        Tina::Math::Vec2 parentWorld = m_parent->getWorldPosition();
        Tina::Math::Vec2 offset = anchorOffset();
        m_worldPos = parentWorld + offset + m_position;
    } else {
        // 根节点：世界坐标 = 局部坐标
        m_worldPos = m_position;
    }

    m_dirty = false;

    // 标记所有子节点为脏（级联更新）
    for (auto* child : m_children) {
        child->m_dirty = true;
    }
}
```

**锚点计算逻辑**：
```cpp
Tina::Math::Vec2 UINode::anchorOffset() const
{
    if (!m_parent) return {0, 0};

    const auto psize = m_parent->getSize();
    float ox = 0, oy = 0;

    switch (m_anchor) {
    case Anchor::TopLeft:       ox = 0;          oy = 0;          break;
    case Anchor::TopCenter:     ox = psize.x/2;  oy = 0;          break;
    case Anchor::TopRight:      ox = psize.x;    oy = 0;          break;
    case Anchor::MiddleLeft:    ox = 0;          oy = psize.y/2;  break;
    case Anchor::MiddleCenter:  ox = psize.x/2;  oy = psize.y/2;  break;
    case Anchor::MiddleRight:   ox = psize.x;    oy = psize.y/2;  break;
    case Anchor::BottomLeft:    ox = 0;          oy = psize.y;    break;
    case Anchor::BottomCenter:  ox = psize.x/2;  oy = psize.y;    break;
    case Anchor::BottomRight:   ox = psize.x;    oy = psize.y;    break;
    }
    return {ox, oy};
}
```

**坐标系示意**：
```
父节点 (200x100)
┌────────────────────┐ (200, 0) TopRight
│  TopLeft           │
│  (0, 0)  MiddleCenter
│           (100, 50)│
│                    │
└────────────────────┘ (200, 100) BottomRight
```

**示例**：
```cpp
UIButton* btn = new UIButton();
btn->setPosition(10, 20);      // 相对父节点偏移
btn->setAnchor(Anchor::MiddleCenter);

// 假设父节点位于 (100, 50)，大小 200x100
// 计算：
// 锚点偏移 = (100, 50)
// 世界坐标 = 100 + 100 + 10, 50 + 50 + 20 = (210, 120)
```

---

##### 3. `containsPoint(worldX, worldY)` - 点测试

```cpp
bool UINode::containsPoint(float worldX, float worldY)
{
    Tina::Math::Vec2 wp = getWorldPosition();
    return worldX >= wp.x && worldX < wp.x + m_size.x &&
           worldY >= wp.y && worldY < wp.y + m_size.y;
}
```

**用途**：
- 事件系统判断鼠标是否在节点内
- 矩形碰撞检测（AABB）

**坐标系**：
- `worldX/Y`：屏幕像素坐标（左上角 = 0,0）
- `wp`：节点左上角世界坐标
- `m_size`：节点宽高

---

##### 4. `render(viewId, renderer)` - 递归渲染

```cpp
void UINode::render(uint16_t viewId, UIRenderer& renderer)
{
    if (!m_visible) return;  // 跳过不可见节点

    onRender(viewId, renderer);  // 1. 渲染自己（虚函数）

    for (auto* child : m_children) {  // 2. 递归渲染子节点
        child->render(viewId, renderer);
    }
}
```

**渲染顺序**：
1. 先绘制父节点（背景）
2. 再绘制子节点（前景）
3. 后绘制的节点在上层

**子类重写示例**：
```cpp
// UIPanel::onRender
void UIPanel::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();
    renderer.drawRect(viewId, pos.x, pos.y, size.x, size.y,
                      m_color.x, m_color.y, m_color.z, m_color.w);
}
```

---

#### 成员变量说明

| 变量 | 类型 | 说明 |
|------|------|------|
| `m_name` | `std::string` | 节点名称（调试用） |
| `m_parent` | `UINode*` | 父节点指针 |
| `m_children` | `Vector<UINode*>` | 子节点列表 |
| `m_position` | `Vec2` | 相对父节点的偏移（像素） |
| `m_size` | `Vec2` | 节点宽高（像素） |
| `m_anchor` | `Anchor` | 锚点对齐方式 |
| `m_worldPos` | `Vec2` | 世界坐标缓存 |
| `m_dirty` | `bool` | 坐标脏标记（需重新计算） |
| `m_visible` | `bool` | 是否可见 |
| `m_enabled` | `bool` | 是否启用（影响事件响应） |

---

### UIComponents（具体组件）

**文件**: `src/ui/UIComponents.hpp`, `src/ui/UIComponents.cpp`

#### UIPanel - 纯色矩形面板

**用途**：背景、容器、分隔线

```cpp
class UIPanel : public UINode {
public:
    void setColor(float r, float g, float b, float a);
    Tina::Math::Vec4 getColor() const;

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    Tina::Math::Vec4 m_color;  // RGBA [0..1]
};
```

**实现细节**：
```cpp
void UIPanel::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();

    // 调用底层渲染器绘制矩形
    renderer.drawRect(viewId, pos.x, pos.y, size.x, size.y,
                      m_color.x, m_color.y, m_color.z, m_color.w);
}
```

**使用示例**：
```cpp
auto* panel = new UIPanel("DebugPanel");
panel->setPosition(10, 10);
panel->setSize(300, 200);
panel->setColor(0.1f, 0.1f, 0.15f, 0.85f);  // 深灰半透明
uiRoot.addChild(panel);
```

---

#### UILabel - 文本标签

**用途**：静态文本、标题、提示

```cpp
class UILabel : public UINode {
public:
    void setText(const std::string& text);
    void setColor(float r, float g, float b, float a);

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    std::string m_text;
    Tina::Math::Vec4 m_color;
};
```

**实现细节**：
```cpp
void UILabel::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();

    // 左上角绘制，带 4px 内边距
    renderer.drawText(viewId, pos.x + 4, pos.y + 4,
                      m_color.x, m_color.y, m_color.z, m_color.w,
                      m_text);
}
```

**使用示例**：
```cpp
auto* label = new UILabel("Title");
label->setPosition(10, 10);
label->setText("调试菜单");
label->setColor(1.0f, 1.0f, 0.5f, 1.0f);  // 黄色
panel->addChild(label);
```

---

#### UIButton - 可点击按钮

**用途**：交互控件、菜单项

```cpp
class UIButton : public UINode {
public:
    void setText(const std::string& text);
    void setNormalColor(float r, g, b, a);
    void setHoverColor(float r, g, b, a);
    void setPressedColor(float r, g, b, a);
    void setTextColor(float r, g, b, a);

    void setHovered(bool h);
    void setPressed(bool p);
    bool isHovered() const;
    bool isPressed() const;

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    std::string m_text;
    Vec4 m_normalColor, m_hoverColor, m_pressedColor, m_textColor;
    bool m_hovered, m_pressed;
};
```

**渲染逻辑**：
```cpp
void UIButton::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();

    // 1. 根据状态选择背景色
    Vec4 bgColor = m_normalColor;
    if (m_pressed) {
        bgColor = m_pressedColor;
    } else if (m_hovered) {
        bgColor = m_hoverColor;
    }

    // 2. 绘制背景
    renderer.drawRect(viewId, pos.x, pos.y, size.x, size.y,
                      bgColor.x, bgColor.y, bgColor.z, bgColor.w);

    // 3. 测量文本宽高
    float tw = 0, th = 0;
    renderer.measureText(m_text, tw, th);

    // 4. 居中绘制文本
    float textX = pos.x + (size.x - tw) * 0.5f;
    float textY = pos.y + (size.y - th) * 0.5f;
    renderer.drawText(viewId, textX, textY,
                      m_textColor.x, m_textColor.y,
                      m_textColor.z, m_textColor.w, m_text);
}
```

**状态机**：
```
Normal (默认)
  ↓ 鼠标进入
Hover (高亮)
  ↓ 鼠标按下
Pressed (按下)
  ↓ 鼠标释放
  → 触发 onClick 回调
  → 回到 Hover
```

**使用示例**：
```cpp
auto* btn = new UIButton("ClearBtn");
btn->setPosition(10, 50);
btn->setSize(200, 40);
btn->setText("清除所有水");
btn->setNormalColor(0.3f, 0.3f, 0.35f, 0.9f);
btn->setHoverColor(0.4f, 0.4f, 0.5f, 0.9f);
btn->setPressedColor(0.2f, 0.2f, 0.25f, 0.9f);

// 绑定回调
btn->onClickCallback = []() {
    TINA_INFO("按钮被点击！");
    // 执行业务逻辑...
};

panel->addChild(btn);
```

---

## 渲染系统

### UIRenderer（UI 渲染管理器）

**文件**: `src/ui/UIRenderer.hpp`, `src/ui/UIRenderer.cpp`

#### 类职责

UI 渲染的高层抽象，协调各渲染组件：
1. 管理 RenderQueue（批处理矩形/精灵）
2. 管理 TextRenderer（专职文本渲染）
3. 提供简化的绘制接口
4. 自动处理渲染作用域（RAII）

#### 核心方法

##### 1. `drawRect()` - 绘制矩形

```cpp
void UIRenderer::drawRect(uint16_t viewId,
                          float x, float y, float w, float h,
                          float r, float g, float b, float a)
{
    // 创建渲染命令并提交到 RenderQueue
    RenderCommand cmd;
    cmd.viewId = viewId;
    cmd.type = RenderType::Rectangle;
    cmd.program = m_shaderManager->getProgram("color");
    cmd.blendMode = BlendMode::Alpha;

    // 设置矩形数据
    cmd.data.rectangle.x = x;
    cmd.data.rectangle.y = y;
    cmd.data.rectangle.width = w;
    cmd.data.rectangle.height = h;
    cmd.data.rectangle.color[0] = r;
    cmd.data.rectangle.color[1] = g;
    cmd.data.rectangle.color[2] = b;
    cmd.data.rectangle.color[3] = a;

    // 提交到批处理队列
    m_renderQueue->submit(cmd);
}
```

**性能优化**：
- ✅ 自动批处理（相同状态的矩形合并为一个 draw call）
- ✅ 索引溢出保护（超过 65536 顶点自动分批）
- ✅ 延迟提交（帧末统一处理）

---

##### 2. `measureText()` - 测量文本

```cpp
bool UIRenderer::measureText(const std::string& utf8,
                              float& outW, float& outH) const
{
    if (!m_text) {
        outW = outH = 0.0f;
        return false;
    }

    // 代理到 TextRenderer
    const_cast<TextRenderer*>(m_text)->measureText(utf8, outW, outH);
    return true;
}
```

**用途**：
- 按钮文本居中
- 自动调整容器大小
- 文本换行计算

---

### RenderQueue（批处理渲染队列）

**文件**: `src/renderer/RenderQueue.hpp`, `src/renderer/RenderQueue.cpp`

#### 类职责

高性能批处理系统，收集并优化渲染命令：
1. 命令收集与排序
2. 状态合并（相同状态的命令批处理）
3. 自动分批（防止索引溢出）
4. 统计信息收集

#### 核心特性

```cpp
class RenderQueue {
    // 批次管理
    Container::Vector<RenderBatch> m_batches;

    // 统计信息
    RenderStats m_stats;

    // 批处理策略
    bool canMerge(const RenderCommand& cmd) const {
        // 检查视图、程序、纹理、混合模式
        // 检查顶点容量（< 65536）
    }
};
```

**性能优势**：
- ✅ Draw Call 减少 90%+（100+ → 5-10）
- ✅ 状态切换最小化
- ✅ 纹理绑定优化
- ✅ CPU-GPU 数据传输优化

---

### TextRenderer（文本渲染器）

**文件**: `src/ui/TextRenderer.hpp`, `src/ui/TextRenderer.cpp`

#### 类职责

完整的文本渲染管线：
1. FreeType 字体加载
2. 字形栅格化
3. 动态图集打包
4. bgfx 网格生成
5. 文本测量

#### 核心方法

##### 1. `loadFont()` - 加载字体

```cpp
bool TextRenderer::loadFont(const std::string& path, int pixelSize)
{
    if (!m_ft) return false;

    // 清理旧字体
    if (m_font.face) {
        FT_Done_Face(m_font.face);
        m_font = {};
    }

    // 加载字体文件
    if (FT_New_Face(m_ft, path.c_str(), 0, &m_font.face)) {
        TINA_ERROR("TextRenderer: 加载字体失败: {}", path);
        return false;
    }

    // 设置 Unicode 字符映射
    FT_Select_Charmap(m_font.face, FT_ENCODING_UNICODE);

    // 设置字体大小（像素）
    FT_Set_Pixel_Sizes(m_font.face, 0, (FT_UInt)pixelSize);

    // 记录字体度量
    m_font.sizePx = pixelSize;
    m_font.ascender = (int)(m_font.face->size->metrics.ascender >> 6);
    m_font.glyphs.clear();

    return true;
}
```

**支持的字体格式**：
- ✅ TrueType (.ttf)
- ✅ OpenType (.otf)
- ✅ 可变字体（Variable Fonts）

---

##### 2. `ensureGlyph()` - 按需加载字形

```cpp
bool TextRenderer::ensureGlyph(Font& font, int codepoint)
{
    // 1. 检查缓存
    if (font.glyphs.find(codepoint) != font.glyphs.end())
        return true;

    // 2. FreeType 栅格化
    if (FT_Load_Char(font.face, (FT_ULong)codepoint,
                     FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT))
        return false;

    FT_GlyphSlot g = font.face->glyph;
    int gw = g->bitmap.width;
    int gh = g->bitmap.rows;

    // 3. 处理空白字符（宽度为 0）
    if (gw == 0 || gh == 0) {
        Glyph gi;
        gi.codepoint = codepoint;
        gi.advance = (int)(g->advance.x >> 6);
        font.glyphs[codepoint] = gi;
        return true;
    }

    // 4. 图集打包（行打包算法）
    if (m_penX + gw + 1 >= m_atlasW) {
        m_penX = 1;
        m_penY += m_rowH + 1;
        m_rowH = 0;
    }
    if (m_penY + gh + 1 >= m_atlasH) {
        TINA_ERROR("TextRenderer: 图集已满 ({}x{})", m_atlasW, m_atlasH);
        return false;
    }

    int dstX = m_penX;
    int dstY = m_penY;
    m_rowH = std::max(m_rowH, gh);
    m_penX += gw + 1;

    // 5. 复制位图到图集
    Tina::Container::Vector<uint8_t> block;
    block.resize((size_t)gw * gh * 4);
    for (int y = 0; y < gh; ++y) {
        const uint8_t* src = g->bitmap.buffer + y * g->bitmap.pitch;
        uint8_t* row = block.data() + y * gw * 4;
        for (int x = 0; x < gw; ++x) {
            row[x*4 + 0] = 255;  // R
            row[x*4 + 1] = 255;  // G
            row[x*4 + 2] = 255;  // B
            row[x*4 + 3] = src[x];  // A (覆盖度)
        }
    }

    // 6. 上传到 GPU
    const bgfx::Memory* mem = bgfx::copy(block.data(), block.size());
    bgfx::updateTexture2D(m_atlasTex, 0, 0,
                          (uint16_t)dstX, (uint16_t)dstY,
                          (uint16_t)gw, (uint16_t)gh, mem);

    // 7. 记录字形信息
    Glyph info;
    info.codepoint = codepoint;
    info.w = gw;
    info.h = gh;
    info.bearingX = g->bitmap_left;
    info.bearingY = g->bitmap_top;
    info.advance = (int)(g->advance.x >> 6);
    info.atlasX = dstX;
    info.atlasY = dstY;
    info.u0 = (float)dstX / m_atlasW;
    info.v0 = (float)dstY / m_atlasH;
    info.u1 = (float)(dstX + gw) / m_atlasW;
    info.v1 = (float)(dstY + gh) / m_atlasH;

    font.glyphs[codepoint] = info;
    return true;
}
```

**图集打包示意**：
```
┌─────────────────────────────────┐ 2048x2048
│ 字 形 A │ 字 形 B │              │
│─────────┼─────────┤              │
│ 字 形 C │ 字 形 D │ 字 形 E      │
│─────────┼─────────┼─────────────│
│                                  │
│          (空闲区域)               │
│                                  │
└─────────────────────────────────┘
```

**性能特点**：
- ✅ 懒加载（只加载用到的字符）
- ✅ 缓存（字形只加载一次）
- ✅ 动态图集（按需扩展）
- ⚠️ 图集满时无法添加新字形

---

##### 3. `measureText()` - 文本测量

```cpp
void TextRenderer::measureText(const std::string& utf8,
                                float& outWidth, float& outHeight)
{
    outWidth = 0.0f;
    outHeight = 0.0f;
    if (!m_font.face || utf8.empty()) return;

    float lineW = 0.0f;
    int lines = 1;
    const char* p = utf8.data();
    const char* end = p + utf8.size();
    int code = 0;

    // 启用字距调整（Kerning）
    const bool hasKerning = (m_font.face && FT_HAS_KERNING(m_font.face));
    FT_UInt prevGlyphIdx = 0;

    // 遍历所有字符
    while (utf8Next(p, end, code)) {
        // 换行符
        if (code == '\n') {
            if (lineW > outWidth) outWidth = lineW;
            lineW = 0.0f;
            ++lines;
            prevGlyphIdx = 0;
            continue;
        }

        // 加载字形（如未缓存）
        if (!ensureGlyph(m_font, code)) continue;

        // Kerning 调整
        if (hasKerning) {
            FT_UInt glyphIdx = FT_Get_Char_Index(m_font.face, code);
            if (prevGlyphIdx != 0 && glyphIdx != 0) {
                FT_Vector delta{};
                if (FT_Get_Kerning(m_font.face, prevGlyphIdx, glyphIdx,
                                   FT_KERNING_DEFAULT, &delta) == 0) {
                    lineW += (float)(delta.x >> 6);
                }
            }
            prevGlyphIdx = glyphIdx;
        }

        // 累加字符前进宽度
        const Glyph& g = m_font.glyphs[code];
        lineW += (float)g.advance;
    }

    // 最后一行
    if (lineW > outWidth) outWidth = lineW;

    // 高度 = 行数 × 字体像素大小
    outHeight = (float)(lines * m_font.sizePx);
}
```

**测量示例**：
```cpp
float w, h;
textRenderer.measureText("清除所有水", w, h);
// w ≈ 140px (5 个中文字符 × 28px)
// h = 28px (单行)

textRenderer.measureText("Line1\nLine2", w, h);
// h = 56px (2 行 × 28px)
```

**注意事项**：
- ✅ 支持多行文本（`\n`）
- ✅ 考虑 Kerning（字距调整）
- ✅ 精确到像素
- ⚠️ 不考虑字形边界（bearing）

---

## 事件系统

### UIEventSystem

**文件**: `src/ui/UIEventSystem.hpp`, `src/ui/UIEventSystem.cpp`

#### 类职责

自动管理鼠标交互：
1. 查找鼠标下的节点
2. 分发 hover/click 事件
3. 维护节点状态

#### 核心方法

##### 1. `updateMouse()` - 更新鼠标状态

```cpp
void UIEventSystem::updateMouse(float mouseX, float mouseY, bool mouseDown)
{
    m_mouseX = mouseX;
    m_mouseY = mouseY;
    m_mouseDownPrev = m_mouseDown;
    m_mouseDown = mouseDown;
}
```

**调用时机**：每帧在事件循环后
```cpp
// main.cpp 主循环
while (running) {
    // ... 处理 SDL 事件 ...

    float mx, my;
    SDL_MouseButtonFlags btn = SDL_GetMouseState(&mx, &my);
    bool leftDown = (btn & SDL_BUTTON_LMASK) != 0;

    uiEventSystem.updateMouse(mx, my, leftDown);
    uiEventSystem.processEvents();  // 分发事件
}
```

---

##### 2. `processEvents()` - 处理事件

```cpp
void UIEventSystem::processEvents()
{
    if (!m_root) return;

    // 1. 查找鼠标下的节点
    UINode* hitNode = findNodeUnderMouse(m_root, m_mouseX, m_mouseY);

    // 2. Hover 事件
    if (hitNode != m_hoveredNode) {
        // Leave
        if (m_hoveredNode) {
            m_hoveredNode->onMouseLeave();
            if (auto* btn = dynamic_cast<UIButton*>(m_hoveredNode)) {
                btn->setHovered(false);
            }
        }

        m_hoveredNode = hitNode;

        // Enter
        if (m_hoveredNode) {
            m_hoveredNode->onMouseEnter();
            if (auto* btn = dynamic_cast<UIButton*>(m_hoveredNode)) {
                btn->setHovered(true);
            }
        }
    }

    // 3. Click 事件
    // 按下时记录
    if (m_mouseDown && !m_mouseDownPrev) {
        m_pressedNode = hitNode;
        if (auto* btn = dynamic_cast<UIButton*>(m_pressedNode)) {
            btn->setPressed(true);
        }
    }

    // 释放时触发
    if (!m_mouseDown && m_mouseDownPrev) {
        if (m_pressedNode && m_pressedNode == hitNode) {
            // 只有在同一节点按下并释放才算点击
            m_pressedNode->onClick();
            if (m_pressedNode->onClickCallback) {
                m_pressedNode->onClickCallback();
            }
        }

        // 清除按下状态
        if (auto* btn = dynamic_cast<UIButton*>(m_pressedNode)) {
            btn->setPressed(false);
        }
        m_pressedNode = nullptr;
    }
}
```

**事件时序**：
```
Frame 1: 鼠标进入按钮
  → findNodeUnderMouse() 返回按钮
  → onMouseEnter() 触发
  → setHovered(true)

Frame 5: 鼠标按下
  → m_mouseDown = true, m_mouseDownPrev = false
  → m_pressedNode = 按钮
  → setPressed(true)

Frame 10: 鼠标释放
  → m_mouseDown = false, m_mouseDownPrev = true
  → hitNode == m_pressedNode
  → onClick() 触发
  → onClickCallback() 执行
  → setPressed(false)
```

---

##### 3. `findNodeUnderMouse()` - 查找节点

```cpp
UINode* UIEventSystem::findNodeUnderMouse(UINode* node, float mx, float my)
{
    if (!node || !node->isVisible() || !node->isEnabled()) {
        return nullptr;
    }

    // 1. 先递归检查子节点（后绘制的在上层）
    const auto& children = node->getChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        UINode* hit = findNodeUnderMouse(*it, mx, my);
        if (hit) return hit;  // 返回最上层的命中节点
    }

    // 2. 再检查当前节点
    if (node->containsPoint(mx, my)) {
        return node;
    }

    return nullptr;
}
```

**遍历顺序**：
```
UI 树：
Root
├─ Panel A
│  └─ Button A1
└─ Panel B
   └─ Button B1

渲染顺序：
1. Root
2. Panel A
3. Button A1
4. Panel B
5. Button B1

查找顺序（反向）：
1. Button B1  ← 最上层
2. Panel B
3. Button A1
4. Panel A
5. Root
```

**优先级规则**：
- ✅ 后绘制的节点优先（Z-order）
- ✅ 子节点优先于父节点
- ✅ 不可见/禁用节点跳过

---

## 使用示例

### 完整示例：创建调试菜单

```cpp
// 1. 创建根节点
Tina::UI::UINode uiRoot("UIRoot");
uiRoot.setPosition(0, 0);
uiRoot.setSize(1280, 720);  // 屏幕尺寸

// 2. 创建面板
auto* debugPanel = new Tina::UI::UIPanel("DebugPanel");
debugPanel->setPosition(10, 100);
debugPanel->setSize(220, 180);
debugPanel->setColor(0.1f, 0.1f, 0.15f, 0.85f);
uiRoot.addChild(debugPanel);

// 3. 创建标题
auto* titleLabel = new Tina::UI::UILabel("TitleLabel");
titleLabel->setPosition(10, 10);  // 相对 debugPanel
titleLabel->setSize(200, 30);
titleLabel->setText("调试菜单");
titleLabel->setColor(1.0f, 1.0f, 0.5f, 1.0f);
debugPanel->addChild(titleLabel);

// 4. 创建按钮 1
auto* btnClearWater = new Tina::UI::UIButton("BtnClearWater");
btnClearWater->setPosition(10, 50);
btnClearWater->setSize(200, 40);
btnClearWater->setText("清除所有水");
btnClearWater->onClickCallback = [&tilemap, &chunkDirtyWater]() {
    TINA_INFO("UI 按钮点击：清除所有水");
    for (int y = 0; y < tilemap.height(); ++y) {
        for (int x = 0; x < tilemap.width(); ++x) {
            tilemap.setWater(x, y, 0);
        }
    }
    for (auto& dirty : chunkDirtyWater) dirty = 1;
};
debugPanel->addChild(btnClearWater);

// 5. 创建按钮 2
auto* btnToggleInfo = new Tina::UI::UIButton("BtnToggleInfo");
btnToggleInfo->setPosition(10, 100);
btnToggleInfo->setSize(200, 40);
btnToggleInfo->setText("切换信息显示");
bool showDetailedInfo = true;
btnToggleInfo->onClickCallback = [&showDetailedInfo]() {
    showDetailedInfo = !showDetailedInfo;
    TINA_INFO("UI 按钮点击：切换信息显示 = {}", showDetailedInfo);
};
debugPanel->addChild(btnToggleInfo);

// 6. 初始化事件系统
Tina::UI::UIEventSystem uiEventSystem;
uiEventSystem.setRoot(&uiRoot);

// 7. 主循环
while (running) {
    // 处理鼠标事件
    float mx, my;
    SDL_MouseButtonFlags btn = SDL_GetMouseState(&mx, &my);
    bool leftDown = (btn & SDL_BUTTON_LMASK) != 0;
    uiEventSystem.updateMouse(mx, my, leftDown);
    uiEventSystem.processEvents();

    // 渲染 UI 树（视图 2 = UI 层）
    uiRoot.render(2, uiRenderer);
}
```

**最终效果**：
```
┌────────────────────┐
│ 调试菜单            │ ← 标题（黄色文本）
├────────────────────┤
│ [清除所有水]        │ ← 按钮 1
├────────────────────┤
│ [切换信息显示]      │ ← 按钮 2
└────────────────────┘
```

---

## 性能优化

### 当前性能特征

| 指标 | 数值 | 说明 |
|------|------|------|
| **Draw Call** | ~5 个/帧 | 1 面板 + 1 标题 + 2 按钮 + 1 中文文本 |
| **顶点数** | ~1000 顶点/帧 | 中文文本较多 |
| **内存占用** | ~8 MB | 2048x2048 图集 |
| **帧率影响** | <0.1 ms | UI 层开销很小 |

### 优化建议

#### 1. 批量渲染（合并 Draw Call）

**当前**：每个矩形 = 1 个 draw call
```cpp
drawRect(...);  // Draw call 1
drawRect(...);  // Draw call 2
drawRect(...);  // Draw call 3
```

**优化后**：累积所有矩形，一次提交
```cpp
class BatchRenderer {
    void begin();
    void addRect(...);  // 累积到缓冲区
    void addText(...);
    void end();         // 一次性提交
};
```

**预期提升**：Draw call 减少 80%+

---

#### 2. 脏标记（减少重绘）

**当前**：每帧全部重绘
```cpp
void render() {
    drawBackground();  // 每帧
    drawText();        // 每帧
}
```

**优化后**：只重绘变化的元素
```cpp
class UINode {
    bool m_renderDirty = true;

    void markDirty() {
        m_renderDirty = true;
        if (m_parent) m_parent->markDirty();
    }

    void render() {
        if (!m_renderDirty) {
            useCache();  // 使用缓存纹理
            return;
        }
        drawToCache();
        m_renderDirty = false;
    }
};
```

**预期提升**：静态 UI 减少 90% GPU 开销

---

#### 3. 图集压缩（减少显存）

**当前**：RGBA8 格式（4 字节/像素）
```
2048x2048 × 4 = 16 MB
```

**优化后**：R8 格式（1 字节/像素）
```
2048x2048 × 1 = 4 MB
```

**实现**：
```cpp
// 着色器修改
vec4 tex = texture2D(s_text, uv);
float alpha = tex.r;  // 只用 R 通道
gl_FragColor = vec4(color.rgb, color.a * alpha);
```

---

## 扩展指南

### 添加新组件

#### 示例：UISlider（滑动条）

```cpp
// UISlider.hpp
class UISlider : public UINode {
public:
    UISlider(const std::string& name = "Slider");

    void setValue(float value);  // [0..1]
    float getValue() const { return m_value; }

    std::function<void(float)> onValueChanged;

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;
    void onUpdate(float dt) override;

private:
    float m_value = 0.5f;
    bool m_dragging = false;
};

// UISlider.cpp
void UISlider::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();

    // 绘制轨道
    renderer.drawRect(viewId, pos.x, pos.y + size.y/2 - 2,
                      size.x, 4, 0.3f, 0.3f, 0.3f, 1.0f);

    // 绘制滑块
    float thumbX = pos.x + m_value * size.x - 8;
    renderer.drawRect(viewId, thumbX, pos.y,
                      16, size.y, 0.6f, 0.6f, 0.7f, 1.0f);
}

void UISlider::onUpdate(float dt)
{
    // 处理拖拽逻辑
    if (m_dragging) {
        // ... 更新 m_value ...
        if (onValueChanged) onValueChanged(m_value);
    }
}
```

**集成**：
```cpp
auto* slider = new UISlider("VolumeSlider");
slider->setPosition(10, 150);
slider->setSize(200, 20);
slider->setValue(0.75f);
slider->onValueChanged = [](float v) {
    TINA_INFO("音量: {}", v * 100.0f);
};
panel->addChild(slider);
```

---

### 集成第三方 UI 库

#### 推荐库

| 库 | 特点 | 集成难度 |
|---|------|---------|
| **Dear ImGui** | 调试工具、编辑器 UI | 简单（已有 bgfx 后端） |
| **Nuklear** | 轻量、单头文件 | 中等 |
| **RmlUI** | HTML/CSS 风格 | 困难 |

#### Dear ImGui 集成示例

```cpp
// 初始化
ImGui::CreateContext();
ImGui_Implbgfx_Init(viewId);

// 主循环
while (running) {
    ImGui_Implbgfx_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Debug Menu");
    if (ImGui::Button("Clear Water")) {
        // ...
    }
    ImGui::End();

    ImGui::Render();
    ImGui_Implbgfx_RenderDrawData(ImGui::GetDrawData());
}
```

---

## 总结

### 系统优势

✅ **架构清晰**：四层分离，职责明确
✅ **易于扩展**：继承 UINode 即可添加新组件
✅ **跨平台**：bgfx + FreeType，全平台支持
✅ **中文友好**：UTF-8 + Kerning + 动态图集
✅ **调试友好**：节点名称、事件日志、可视化工具

### 适用场景

✅ 游戏 HUD（生命值、得分、小地图）
✅ 调试菜单（性能监控、参数调整）
✅ 暂停菜单（简单选项）
⚠️ 不适合复杂表单/编辑器（建议用 ImGui）

### 已知限制

- ⚠️ 图集固定大小（2048x2048）
- ⚠️ 单字体支持（可扩展多字体）
- ⚠️ 无布局系统（手动定位）
- ⚠️ 无动画系统（可手动实现）

---

## 参考资料

- [bgfx 官方文档](https://bkaradzic.github.io/bgfx/)
- [FreeType 教程](https://freetype.org/freetype2/docs/tutorial/)
- [Dear ImGui 官方](https://github.com/ocornut/imgui)
- [Tina 引擎设计文档](engine_design_proposal.md)

---

## 批处理优化系统（2025-10更新）

### RAII 渲染作用域

UIRenderer 提供 RAII 作用域，自动管理 beginFrame/flush：

```cpp
void MyScene::render() {
    auto scope = ui().beginRender(uiViewId());
    m_rootNode->render(uiViewId(), ui());
    // scope 析构时自动 flush
}
```

### 性能统计

```cpp
ui().setStatsEnabled(true);  // 启用统计
const auto& stats = ui().getLastFrameStats();
```

---

## Signal 事件系统（2025-10更新）

UIButton 使用 Signal 事件：

```cpp
m_btnConnection = btn->onClick.connect([this]() {
    onStartClicked();
});
```

UINode 拥有子节点所有权：

```cpp
auto* btn = m_rootNode->createChild<UI::UIButton>("Btn");
```

---

## 相关文档

- [scene_system.md](scene_system.md) - Scene 系统架构
- [scene-best-practices.md](scene-best-practices.md) - Scene 开发最佳实践

---

**文档版本**: 1.1
**最后更新**: 2025-10-10
**维护者**: Tina 引擎团队