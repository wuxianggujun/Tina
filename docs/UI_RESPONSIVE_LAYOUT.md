# UI 响应式布局系统

## 🎯 问题背景

**之前的问题：**
- ❌ UI尺寸写死（如 `dialogW = 500`）
- ❌ 小屏幕显示不全
- ❌ 大屏幕显得很小
- ❌ 窗口最大化后UI不会放大

**现在的解决方案：**
- ✅ 百分比尺寸（相对于父节点）
- ✅ 最小/最大尺寸约束
- ✅ 自动响应窗口尺寸变化
- ✅ 完美的响应式布局

## 📚 API 完整列表

### 1. 固定尺寸（像素）

```cpp
// 传统方式：固定像素尺寸
panel->setSize(500, 300);
```

### 2. 百分比尺寸（响应式）

```cpp
// 百分比尺寸：相对于父节点
panel->setSizePercent(0.5f, 0.4f);  // 50%宽，40%高
```

### 3. 尺寸约束

```cpp
// 设置最小尺寸（防止太小）
panel->setMinSize(400, 300);

// 设置最大尺寸（防止太大）
panel->setMaxSize(800, 600);
```

### 4. 组合使用

```cpp
// 完美的响应式布局
panel->setSizePercent(0.5f, 0.4f)  // 50%宽，40%高
     ->setMinSize(400, 300)         // 最小400x300
     ->setMaxSize(800, 600)         // 最大800x600
     ->center();                    // 居中显示
```

## 🎨 实际应用场景

### 场景1：响应式对话框

```cpp
// ✅ 响应式对话框：自动适应屏幕大小
auto dialog = createChild<UIPanel>("Dialog");
dialog->setSizePercent(0.4f, 0.3f)  // 占屏幕40%宽，30%高
      ->setMinSize(400, 300)         // 最小尺寸
      ->setMaxSize(800, 600)         // 最大尺寸
      ->center()
      ->setColor(0.1f, 0.1f, 0.15f, 0.95f);

// 效果：
// - 小屏幕（1280x720）：400x300（最小尺寸）
// - 中屏幕（1920x1080）：768x324
// - 大屏幕（3840x2160）：800x600（最大尺寸）
```

### 场景2：全屏遮罩层

```cpp
// ✅ 全屏遮罩：始终填满整个屏幕
auto mask = createChild<UIPanel>("Mask");
mask->setSizePercent(1.0f, 1.0f)  // 100%宽，100%高
    ->setColor(0, 0, 0, 0.5f);     // 半透明黑色

// 效果：无论窗口多大，遮罩层都会填满
```

### 场景3：侧边栏

```cpp
// ✅ 响应式侧边栏：高度跟随屏幕，宽度固定
auto sidebar = createChild<UIPanel>("Sidebar");
sidebar->setWidth(200)              // 固定宽度200px
       ->setSizePercent(0, 1.0f)    // 高度100%（宽度被setWidth覆盖）
       ->alignLeft()
       ->setColor(0.12f, 0.12f, 0.15f, 1.0f);

// 或者更简单的方式：
sidebar->setSize(200, 0)            // 宽度200px，高度0（会被百分比覆盖）
       ->setSizePercent(0, 1.0f)    // 只设置高度为100%
       ->alignLeft();
```

### 场景4：响应式断点

```cpp
// ✅ 根据屏幕大小使用不同的布局
void onWindowSizeChanged(int width, int height) override {
    if (width < 800) {
        // 小屏幕：占90%，垂直布局
        dialog->setSizePercent(0.9f, 0.8f)
              ->setMinSize(300, 400);
    } else if (width < 1600) {
        // 中屏幕：占60%
        dialog->setSizePercent(0.6f, 0.5f)
              ->setMinSize(400, 300);
    } else {
        // 大屏幕：占40%
        dialog->setSizePercent(0.4f, 0.3f)
              ->setMinSize(600, 400)
              ->setMaxSize(1000, 800);
    }
}
```

### 场景5：网格布局

```cpp
// ✅ 响应式网格：每个格子占25%
for (int i = 0; i < 4; i++) {
    auto cell = container->createChild<UIPanel>("Cell" + std::to_string(i));
    cell->setSizePercent(0.25f, 1.0f)  // 25%宽，100%高
        ->setPosition(i * containerW * 0.25f, 0);
}
```

## 🔧 工作原理

### 尺寸计算流程

```cpp
// 1. 设置百分比尺寸
panel->setSizePercent(0.5f, 0.4f);

// 2. 计算实际尺寸
float actualW = parentWidth * 0.5f;   // 50%宽
float actualH = parentHeight * 0.4f;  // 40%高

// 3. 应用约束
actualW = std::max(minW, std::min(maxW, actualW));
actualH = std::max(minH, std::min(maxH, actualH));

// 4. 更新节点尺寸
m_size = {actualW, actualH};
```

### 自动更新机制

```cpp
// 窗口尺寸变化时
void onWindowSizeChanged(int width, int height) {
    // 1. 更新根节点尺寸
    rootNode->setSize(width, height);
    
    // 2. 递归更新所有子节点
    for (auto& child : children) {
        child->updatePercentSize();  // ✅ 重新计算百分比尺寸
        child->onWindowSizeChanged(width, height);
    }
}
```

## 📊 尺寸模式对比

| 模式 | 设置方式 | 响应式 | 适用场景 |
|------|---------|--------|---------|
| **固定尺寸** | `setSize(500, 300)` | ❌ | 固定大小的UI元素（如图标、按钮） |
| **百分比尺寸** | `setSizePercent(0.5f, 0.4f)` | ✅ | 需要适应屏幕的UI（如对话框、面板） |
| **百分比+约束** | `setSizePercent(0.5f, 0.4f)->setMinSize(400, 300)` | ✅ | 响应式但有尺寸限制的UI |

## 💡 最佳实践

### 1. 对话框使用百分比+约束

```cpp
// ✅ 推荐：响应式对话框
dialog->setSizePercent(0.4f, 0.3f)
      ->setMinSize(400, 300)
      ->setMaxSize(800, 600)
      ->center();

// ❌ 不推荐：固定尺寸
dialog->setSize(500, 300)->center();
```

### 2. 全屏元素使用100%

```cpp
// ✅ 推荐：全屏遮罩
mask->setSizePercent(1.0f, 1.0f);

// ❌ 不推荐：手动设置屏幕尺寸
mask->setSize(screenW, screenH);  // 窗口变化时需要手动更新
```

### 3. 固定尺寸用于小元素

```cpp
// ✅ 推荐：按钮使用固定尺寸
button->setSize(120, 40);

// ❌ 不推荐：按钮使用百分比（会太大或太小）
button->setSizePercent(0.1f, 0.05f);
```

### 4. 组合使用固定和百分比

```cpp
// ✅ 推荐：侧边栏宽度固定，高度100%
sidebar->setWidth(200)
       ->setSizePercent(0, 1.0f)  // 只设置高度为100%
       ->alignLeft();
```

## 🎯 响应式布局策略

### 策略1：内容优先

```cpp
// 根据内容大小设置最小尺寸
dialog->setSizePercent(0.5f, 0.4f)
      ->setMinSize(contentWidth + padding, contentHeight + padding);
```

### 策略2：断点设计

```cpp
// 定义响应式断点
const int BREAKPOINT_SMALL = 800;
const int BREAKPOINT_MEDIUM = 1600;

void updateLayout(int screenWidth) {
    if (screenWidth < BREAKPOINT_SMALL) {
        // 小屏幕布局
        panel->setSizePercent(0.9f, 0.8f);
    } else if (screenWidth < BREAKPOINT_MEDIUM) {
        // 中屏幕布局
        panel->setSizePercent(0.6f, 0.5f);
    } else {
        // 大屏幕布局
        panel->setSizePercent(0.4f, 0.3f);
    }
}
```

### 策略3：比例保持

```cpp
// 保持16:9比例
float aspectRatio = 16.0f / 9.0f;
panel->setSizePercent(0.6f, 0.6f / aspectRatio)
     ->setMaxSize(1920, 1080);
```

## 🚀 实际效果

### 小屏幕（1280x720）

```
对话框尺寸：400x300（最小尺寸）
占屏幕比例：31.25% x 41.67%
```

### 中屏幕（1920x1080）

```
对话框尺寸：768x324（40% x 30%）
占屏幕比例：40% x 30%
```

### 大屏幕（3840x2160）

```
对话框尺寸：800x600（最大尺寸）
占屏幕比例：20.83% x 27.78%
```

## ⚠️ 注意事项

### 1. 百分比基于父节点

```cpp
// ✅ 正确：百分比相对于父节点
container->setSize(800, 600);
child->setSizePercent(0.5f, 0.5f);  // 400x300

// ❌ 错误：百分比不是相对于屏幕
// 如果需要相对于屏幕，父节点应该是全屏的
```

### 2. 约束优先级

```cpp
// 约束优先级：min > percent > max
panel->setSizePercent(0.5f, 0.5f)  // 计算：960x540
     ->setMinSize(1000, 600)        // 应用最小：1000x600
     ->setMaxSize(800, 500);        // 应用最大：800x500（最终）
```

### 3. 性能考虑

```cpp
// ✅ 推荐：只在需要时使用百分比
dialog->setSizePercent(0.4f, 0.3f);  // 对话框需要响应式

// ✅ 推荐：小元素使用固定尺寸
button->setSize(120, 40);  // 按钮不需要响应式
```

## 🎉 总结

响应式布局系统提供了：

- ✅ **百分比尺寸** - 相对于父节点的尺寸
- ✅ **尺寸约束** - 最小/最大尺寸限制
- ✅ **自动更新** - 窗口变化时自动重新计算
- ✅ **灵活组合** - 可以混合使用固定和百分比
- ✅ **性能优化** - 只在需要时重新计算

现在你的UI可以完美适应任何屏幕尺寸了！🚀
