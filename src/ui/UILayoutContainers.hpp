//
// UI 布局容器
// - UIVBox: 垂直布局容器（子元素自动垂直排列）
// - UIHBox: 水平布局容器（子元素自动水平排列）
// - UIGrid: 网格布局容器（子元素按网格排列）
//

#pragma once

#include "UINode.hpp"
#include "../core/Log.hpp"  // ✅ 添加日志支持
#include <algorithm>
#include <limits>

namespace Tina::UI {

// ============================================================================
// UIVBox - 垂直布局容器
// ============================================================================
// 
// 功能：
// - 子元素自动垂直排列
// - 子元素宽度自动填充（可选）
// - 支持间距和内边距
// - 支持对齐方式
//
// 使用示例：
//   auto vbox = createChild<UIVBox>("VBox");
//   vbox->setSpacing(20);           // 子元素间距20px
//   vbox->setPadding(20);           // 内边距20px
//   vbox->setFillWidth(true);       // 子元素宽度自动填充
//   
//   auto label = vbox->addChild<UILabel>("Label");
//   label->setHeight(30);           // 只需设置高度
//   
//   auto input = vbox->addChild<UITextEdit>("Input");
//   input->setHeight(40);
//
class UIVBox : public UINode {
public:
    UIVBox(const std::string& name = "VBox")
        : UINode(name)
    {}

    // === 布局配置 ===
    
    // 设置子元素间距
    UIVBox* setSpacing(float spacing) {
        m_spacing = spacing;
        requestLayout();
        return this;
    }
    
    // 设置内边距（统一）
    UIVBox* setPadding(float padding) {
        m_paddingL = m_paddingT = m_paddingR = m_paddingB = padding;
        requestLayout();
        return this;
    }
    
    // 设置内边距（分别）
    UIVBox* setPadding(float left, float top, float right, float bottom) {
        m_paddingL = left;
        m_paddingT = top;
        m_paddingR = right;
        m_paddingB = bottom;
        requestLayout();
        return this;
    }
    
    // 设置子元素是否自动填充宽度
    UIVBox* setFillWidth(bool fill) {
        m_fillWidth = fill;
        requestLayout();
        return this;
    }
    
    // 设置子元素水平对齐方式
    UIVBox* setChildAlign(HAlign align) {
        m_childAlign = align;
        requestLayout();
        return this;
    }
    
    float getSpacing() const { return m_spacing; }
    bool isFillWidth() const { return m_fillWidth; }
    
    // ✅ 重写setSize，自动标记需要布局
    UIVBox* setSize(float w, float h) {
        if (m_size.x != w || m_size.y != h) {
            UINode::setSize(w, h);
            requestLayout();  // ✅ 自动标记dirty
        }
        return this;
    }
    
    // ✅ 重写performLayoutNow，直接调用onLayout
    void performLayoutNow() override {
        if (!needsLayout()) return;
        m_layouting = false;  // 重置标志
        onLayout();
        m_layoutDirty = false;  // 清除dirty标志
    }

protected:
    // 测量：计算包裹尺寸（考虑padding/spacing/margin）
    Tina::Math::Vec2 measureContent(float availableWidth, float availableHeight) override {
        (void)availableHeight;
        float contentWidth = availableWidth - m_paddingL - m_paddingR;
        if (contentWidth < 0) contentWidth = 0;

        float totalH = m_paddingT + m_paddingB;
        float maxChildW = 0.0f;
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            float ml = child->marginLeft();
            float mr = child->marginRight();
            float mt = child->marginTop();
            float mb = child->marginBottom();
            float childAvailW = std::max(0.0f, contentWidth - ml - mr);
            auto ms = child->measure(childAvailW, std::numeric_limits<float>::infinity());
            totalH += mt + ms.y + mb + m_spacing;
            float totalChildW = ml + ms.x + mr;
            if (totalChildW > maxChildW) maxChildW = totalChildW;
        }
        if (!m_children.empty()) totalH -= m_spacing;
        float desiredW = maxChildW + m_paddingL + m_paddingR;
        return {desiredW, totalH};
    }

    void onLayout() override {
        if (m_layouting) return;  // ✅ 避免递归
        m_layouting = true;

        // WrapContent：使用测量结果（仅影响包裹轴）
        if (layoutHeight() == LayoutDim::WrapContent) {
            measure(m_size.x, std::numeric_limits<float>::infinity());
            applyMeasuredHeight();
        }

        float contentWidth = m_size.x - m_paddingL - m_paddingR;
        float contentHeight = m_size.y - m_paddingT - m_paddingB;

        // 第一遍：统计固定高度与 MatchParent 个数
        float fixedHeight = 0.0f;
        int matchCount = 0;
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            if (child->layoutHeight() == LayoutDim::MatchParent) {
                matchCount++;
            } else {
                fixedHeight += child->marginTop() + child->getSize().y + child->marginBottom() + m_spacing;
            }
        }
        if (fixedHeight > 0 && !m_children.empty()) fixedHeight -= m_spacing; // 去掉最后一次spacing
        float remaining = std::max(0.0f, contentHeight - fixedHeight);
        float eachMatchH = (matchCount > 0) ? std::max(0.0f, remaining - m_spacing * std::max(0, matchCount - 1)) / matchCount : 0.0f;

        // 第二遍：实际布局
        float y = m_paddingT;
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;

            float ml = child->marginLeft();
            float mr = child->marginRight();
            float mt = child->marginTop();
            float mb = child->marginBottom();

            float childW = child->getSize().x;
            float childH = child->getSize().y;

            if (m_fillWidth || child->layoutWidth() == LayoutDim::MatchParent) {
                childW = std::max(0.0f, contentWidth - ml - mr);
                child->setWidth(childW);
                child->requestLayout();
            }
            if (child->layoutHeight() == LayoutDim::MatchParent) {
                childH = std::max(0.0f, eachMatchH - mt - mb);
                child->setHeight(childH);
                child->requestLayout();
            }

            float x = m_paddingL + ml;
            switch (m_childAlign) {
                case HAlign::Left:   x = m_paddingL + ml; break;
                case HAlign::Center: x = m_paddingL + (contentWidth - (childW + ml + mr)) * 0.5f + ml; break;
                case HAlign::Right:  x = m_paddingL + (contentWidth - (childW + ml + mr)) + ml; break;
            }

            y += mt;
            child->setPosition(x, y);
            y += childH + mb + m_spacing;
        }

        m_layouting = false;  // ✅ 重置标志
    }

private:
    float m_spacing = 0.0f;
    float m_paddingL = 0.0f, m_paddingT = 0.0f;
    float m_paddingR = 0.0f, m_paddingB = 0.0f;
    bool m_fillWidth = true;          // 默认子元素宽度填充
    HAlign m_childAlign = HAlign::Left;
    bool m_layouting = false;         // ✅ 布局标志，防止递归
};

// ============================================================================
// UIHBox - 水平布局容器
// ============================================================================
//
// 功能：
// - 子元素自动水平排列
// - 子元素高度自动填充（可选）
// - 支持间距和内边距
// - 支持对齐和分布方式
//
// 使用示例：
//   auto hbox = createChild<UIHBox>("HBox");
//   hbox->setSpacing(20);
//   hbox->setJustify(UIHBox::Justify::Center);  // 居中对齐
//   
//   auto btn1 = hbox->addChild<UIButton>("Btn1");
//   btn1->setSize(120, 40);
//   
//   auto btn2 = hbox->addChild<UIButton>("Btn2");
//   btn2->setSize(120, 40);
//
class UIHBox : public UINode {
public:
    // 水平分布方式
    enum class Justify {
        Start,          // 左对齐
        Center,         // 居中
        End,            // 右对齐
        SpaceBetween,   // 两端对齐，元素间距相等
        SpaceAround,    // 元素周围间距相等
        SpaceEvenly     // 所有间距相等
    };
    
    UIHBox(const std::string& name = "HBox")
        : UINode(name)
    {}

    // === 布局配置 ===
    
    UIHBox* setSpacing(float spacing) {
        m_spacing = spacing;
        requestLayout();
        return this;
    }
    
    UIHBox* setPadding(float padding) {
        m_paddingL = m_paddingT = m_paddingR = m_paddingB = padding;
        requestLayout();
        return this;
    }
    
    UIHBox* setPadding(float left, float top, float right, float bottom) {
        m_paddingL = left;
        m_paddingT = top;
        m_paddingR = right;
        m_paddingB = bottom;
        requestLayout();
        return this;
    }
    
    UIHBox* setFillHeight(bool fill) {
        m_fillHeight = fill;
        requestLayout();
        return this;
    }
    
    UIHBox* setJustify(Justify justify) {
        m_justify = justify;
        requestLayout();
        return this;
    }
    
    UIHBox* setChildAlign(VAlign align) {
        m_childAlign = align;
        requestLayout();
        return this;
    }
    
    // ✅ 重写setSize，自动标记需要布局
    UIHBox* setSize(float w, float h) {
        if (m_size.x != w || m_size.y != h) {
            UINode::setSize(w, h);
            requestLayout();  // ✅ 自动标记dirty
        }
        return this;
    }
    
    // ✅ 重写performLayoutNow，直接调用onLayout
    void performLayoutNow() override {
        if (!needsLayout()) return;
        m_layouting = false;  // 重置标志
        onLayout();
        m_layoutDirty = false;  // 清除dirty标志
    }

protected:
    // 测量：计算包裹尺寸（考虑padding/spacing/margin）
    Tina::Math::Vec2 measureContent(float availableWidth, float availableHeight) override {
        (void)availableHeight;
        float contentWidth = availableWidth - m_paddingL - m_paddingR;
        if (contentWidth < 0) contentWidth = 0;
        float totalW = m_paddingL + m_paddingR;
        float maxH = 0.0f;
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            float ml = child->marginLeft();
            float mr = child->marginRight();
            float mt = child->marginTop();
            float mb = child->marginBottom();
            auto ms = child->measure(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
            totalW += ml + ms.x + mr + m_spacing;
            float childTotalH = mt + ms.y + mb;
            if (childTotalH > maxH) maxH = childTotalH;
        }
        if (!m_children.empty()) totalW -= m_spacing;
        float desiredW = totalW;
        float desiredH = maxH + m_paddingT + m_paddingB;
        return {desiredW, desiredH};
    }

    void onLayout() override {
        if (m_layouting) return;  // ✅ 避免递归
        m_layouting = true;
        // WrapContent：使用测量结果
        if (layoutWidth() == LayoutDim::WrapContent) {
            measure(std::numeric_limits<float>::infinity(), m_size.y);
            applyMeasuredWidth();
        }

        float contentWidth = m_size.x - m_paddingL - m_paddingR;
        float contentHeight = m_size.y - m_paddingT - m_paddingB;
        
        // 计算所有可见子元素的总宽度
        float totalChildWidth = 0;
        int visibleCount = 0;
        int matchCount = 0;
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            if (child->layoutWidth() == LayoutDim::MatchParent) {
                matchCount++;
            } else {
                totalChildWidth += child->getSize().x;
            }
            visibleCount++;
        }
        
        if (visibleCount == 0) {
            m_layouting = false;  // ✅ 重置标志
            return;
        }
        // 预分配 MatchParent 宽度（等分剩余空间，考虑间距）
        float fixedWidth = totalChildWidth + m_spacing * (visibleCount - 1);
        float remaining = std::max(0.0f, contentWidth - fixedWidth);
        float eachMatchW = (matchCount > 0) ? remaining / matchCount : 0.0f;

        // 计算起始位置和间距
        float x = m_paddingL;
        float spacing = m_spacing;
        
        switch (m_justify) {
            case Justify::Start:
                x = m_paddingL;
                break;
            case Justify::Center:
                x = m_paddingL + (contentWidth - (totalChildWidth + spacing * (visibleCount - 1) + eachMatchW * matchCount)) * 0.5f;
                break;
            case Justify::End:
                x = m_paddingL + (contentWidth - (totalChildWidth + spacing * (visibleCount - 1) + eachMatchW * matchCount));
                break;
            case Justify::SpaceBetween:
                x = m_paddingL;
                if (visibleCount > 1) {
                    spacing = (contentWidth - (totalChildWidth + eachMatchW * matchCount)) / (visibleCount - 1);
                }
                break;
            case Justify::SpaceAround:
                spacing = (contentWidth - (totalChildWidth + eachMatchW * matchCount)) / visibleCount;
                x = m_paddingL + spacing * 0.5f;
                break;
            case Justify::SpaceEvenly:
                spacing = (contentWidth - (totalChildWidth + eachMatchW * matchCount)) / (visibleCount + 1);
                x = m_paddingL + spacing;
                break;
        }
        
        // 布局子元素
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            
            auto childSize = child->getSize();
            float childWidth = childSize.x;
            
            // ✅ 支持WidthMatch：子元素填充剩余宽度
            if (child->layoutWidth() == LayoutDim::MatchParent) {
                childWidth = std::max(0.0f, eachMatchW);
                child->setWidth(childWidth);
                child->requestLayout();
            }
            
            float childHeight = childSize.y;
            
            // ✅ 支持HeightMatch：子元素填充父容器高度
            if (m_fillHeight || child->layoutHeight() == LayoutDim::MatchParent) {
                childHeight = contentHeight;
                child->setHeight(childHeight);
                child->requestLayout();
            }
            
            // 计算垂直位置（根据对齐方式）
            float y = m_paddingT;
            switch (m_childAlign) {
                case VAlign::Top:
                    y = m_paddingT;
                    break;
                case VAlign::Middle:
                    y = m_paddingT + (contentHeight - childHeight) * 0.5f;
                    break;
                case VAlign::Bottom:
                    y = m_paddingT + (contentHeight - childHeight);
                    break;
            }
            
            child->setPosition(x, y);
            // 不在这里递归触发布局，交由布局管理器批处理
            
            x += childWidth + spacing;
        }
        
        m_layouting = false;  // ✅ 重置标志
    }

private:
    float m_spacing = 0.0f;
    float m_paddingL = 0.0f, m_paddingT = 0.0f;
    float m_paddingR = 0.0f, m_paddingB = 0.0f;
    bool m_fillHeight = false;        // 默认子元素高度不填充
    Justify m_justify = Justify::Start;
    VAlign m_childAlign = VAlign::Middle;
    bool m_layouting = false;         // ✅ 布局标志，防止递归
};

// ============================================================================
// UIGrid - 网格布局容器
// ============================================================================
//
// 功能：
// - 子元素按网格排列
// - 支持固定列数或固定列宽
// - 支持行列间距
// - 支持单元格对齐
//
// 使用示例：
//   auto grid = createChild<UIGrid>("Grid");
//   grid->setColumns(3);            // 3列
//   grid->setSpacing(10, 10);       // 行列间距
//   
//   for (int i = 0; i < 9; i++) {
//       auto cell = grid->addChild<UIPanel>("Cell" + std::to_string(i));
//       cell->setSize(100, 100);
//   }
//
class UIGrid : public UINode {
public:
    UIGrid(const std::string& name = "Grid")
        : UINode(name)
    {}

    // === 布局配置 ===
    
    // 设置列数（固定列数模式）
    UIGrid* setColumns(int columns) {
        m_columns = columns;
        m_fixedColumns = true;
        requestLayout();
        return this;
    }
    
    // 设置列宽（固定列宽模式）
    UIGrid* setColumnWidth(float width) {
        m_columnWidth = width;
        m_fixedColumns = false;
        requestLayout();
        return this;
    }
    
    // 设置行列间距
    UIGrid* setSpacing(float horizontal, float vertical) {
        m_spacingH = horizontal;
        m_spacingV = vertical;
        requestLayout();
        return this;
    }
    
    UIGrid* setPadding(float padding) {
        m_paddingL = m_paddingT = m_paddingR = m_paddingB = padding;
        requestLayout();
        return this;
    }
    
    // 设置单元格对齐方式
    UIGrid* setCellAlign(HAlign hAlign, VAlign vAlign) {
        m_cellHAlign = hAlign;
        m_cellVAlign = vAlign;
        requestLayout();
        return this;
    }
    
    // ✅ 重写setSize，自动标记需要布局
    UIGrid* setSize(float w, float h) {
        if (m_size.x != w || m_size.y != h) {
            UINode::setSize(w, h);
            requestLayout();  // ✅ 自动标记dirty
        }
        return this;
    }
    
    // ✅ 重写performLayoutNow，直接调用onLayout
    void performLayoutNow() override {
        if (!needsLayout()) return;
        m_layouting = false;  // 重置标志
        onLayout();
        m_layoutDirty = false;  // 清除dirty标志
    }

protected:
    // 测量：根据可用宽度与列数/列宽预估网格所需尺寸
    Tina::Math::Vec2 measureContent(float availableWidth, float availableHeight) override {
        (void)availableHeight;
        float contentWidth = availableWidth - m_paddingL - m_paddingR;
        if (contentWidth < 1.0f) contentWidth = 1.0f;

        // 计算列数与单元格宽度
        int columns = m_columns;
        if (!m_fixedColumns && m_columnWidth > 0) {
            columns = static_cast<int>((contentWidth + m_spacingH) / (m_columnWidth + m_spacingH));
            columns = std::max(1, columns);
        }
        if (columns <= 0) columns = 1;
        float cellWidth = (contentWidth - m_spacingH * (columns - 1)) / columns;

        // 逐个测量子项，计算行高
        Tina::Container::Vector<float> rowHeights;
        int index = 0;
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            int row = index / columns;
            if (row >= (int)rowHeights.size()) rowHeights.push_back(0.0f);

            float ml = child->marginLeft();
            float mr = child->marginRight();
            float mt = child->marginTop();
            float mb = child->marginBottom();
            float childAvailW = std::max(0.0f, cellWidth - ml - mr);
            auto ms = child->measure(childAvailW, std::numeric_limits<float>::infinity());
            float totalH = mt + ms.y + mb;
            if (totalH > rowHeights[row]) rowHeights[row] = totalH;
            ++index;
        }

        float totalHeight = m_paddingT + m_paddingB;
        for (size_t r = 0; r < rowHeights.size(); ++r) {
            if (r > 0) totalHeight += m_spacingV;
            totalHeight += rowHeights[r];
        }

        float desiredWidth = m_paddingL + m_paddingR + columns * cellWidth + m_spacingH * (columns - 1);
        return {desiredWidth, totalHeight};
    }

    void onLayout() override {
        if (m_layouting) return;  // ✅ 避免递归
        m_layouting = true;
        
        float contentWidth = m_size.x - m_paddingL - m_paddingR;
        float contentHeight = m_size.y - m_paddingT - m_paddingB;
        
        // 计算列数
        int columns = m_columns;
        if (!m_fixedColumns && m_columnWidth > 0) {
            columns = static_cast<int>((contentWidth + m_spacingH) / (m_columnWidth + m_spacingH));
            columns = std::max(1, columns);
        }
        
        if (columns <= 0) {
            m_layouting = false;  // ✅ 重置标志
            return;
        }
        
        // 计算单元格尺寸
        float cellWidth = (contentWidth - m_spacingH * (columns - 1)) / columns;
        // 收集可见子节点
        Tina::Container::Vector<UINode*> items;
        items.reserve(m_children.size());
        for (auto& c : m_children) if (c && c->isVisible()) items.push_back(c.get());

        // 计算每行最大高度
        int count = static_cast<int>(items.size());
        int rows = (count + columns - 1) / columns;
        Tina::Container::Vector<float> rowHeights;
        rowHeights.resize(rows, 0.0f);
        for (int i = 0; i < count; ++i) {
            int row = i / columns;
            auto* ch = items[i];
            float mt = ch->marginTop();
            float mb = ch->marginBottom();
            auto sz = ch->getSize();
            float total = mt + sz.y + mb;
            if (total > rowHeights[row]) rowHeights[row] = total;
        }

        // 布局子元素（使用行高做垂直对齐）
        float y = m_paddingT;
        for (int i = 0; i < count; ++i) {
            int col = i % columns;
            int row = i / columns;
            if (col == 0 && i > 0) y += rowHeights[row - 1] + m_spacingV;

            float x = m_paddingL + col * (cellWidth + m_spacingH);
            auto* child = items[i];
            auto sz = child->getSize();
            float ml = child->marginLeft();
            float mr = child->marginRight();
            float mt = child->marginTop();
            float mb = child->marginBottom();
            float childWidth = sz.x;
            float childHeight = sz.y;

            float cellX = x;
            float cellY = y;
            switch (m_cellHAlign) {
                case HAlign::Left:   cellX = x + ml; break;
                case HAlign::Center: cellX = x + (cellWidth - (childWidth + ml + mr)) * 0.5f + ml; break;
                case HAlign::Right:  cellX = x + (cellWidth - (childWidth + ml + mr)) + ml; break;
            }
            switch (m_cellVAlign) {
                case VAlign::Top:    cellY = y + mt; break;
                case VAlign::Middle: cellY = y + (rowHeights[row] - (childHeight + mt + mb)) * 0.5f + mt; break;
                case VAlign::Bottom: cellY = y + (rowHeights[row] - (childHeight + mt + mb)) + mt; break;
            }
            child->setPosition(cellX, cellY);
        }
        
        m_layouting = false;  // ✅ 重置标志
    }

private:
    int m_columns = 1;
    float m_columnWidth = 100.0f;
    bool m_fixedColumns = true;       // true: 固定列数, false: 固定列宽
    float m_spacingH = 0.0f;          // 水平间距
    float m_spacingV = 0.0f;          // 垂直间距
    float m_paddingL = 0.0f, m_paddingT = 0.0f;
    float m_paddingR = 0.0f, m_paddingB = 0.0f;
    HAlign m_cellHAlign = HAlign::Left;
    VAlign m_cellVAlign = VAlign::Top;
    bool m_layouting = false;         // ✅ 布局标志，防止递归
};

} // namespace Tina::UI
