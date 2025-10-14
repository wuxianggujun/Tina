//
// UI 布局容器
// - UIVBox: 垂直布局容器（子元素自动垂直排列）
// - UIHBox: 水平布局容器（子元素自动水平排列）
// - UIGrid: 网格布局容器（子元素按网格排列）
//

#pragma once

#include "UINode.hpp"

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
    
    // ✅ 重写setSize，在尺寸变化时触发布局
    UIVBox* setSize(float w, float h) {
        UINode::setSize(w, h);
        onLayout();  // 尺寸变化时重新布局
        return this;
    }

protected:
    void onLayout() override {
        float y = m_paddingT;
        float contentWidth = m_size.x - m_paddingL - m_paddingR;
        
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            
            auto childSize = child->getSize();
            float childWidth = m_fillWidth ? contentWidth : childSize.x;
            float childHeight = childSize.y;
            
            // 计算水平位置（根据对齐方式）
            float x = m_paddingL;
            switch (m_childAlign) {
                case HAlign::Left:
                    x = m_paddingL;
                    break;
                case HAlign::Center:
                    x = m_paddingL + (contentWidth - childWidth) * 0.5f;
                    break;
                case HAlign::Right:
                    x = m_paddingL + (contentWidth - childWidth);
                    break;
            }
            
            // 设置子元素位置和尺寸
            child->setPosition(x, y);
            if (m_fillWidth) {
                child->setWidth(childWidth);
            }
            
            y += childHeight + m_spacing;
        }
    }

private:
    float m_spacing = 0.0f;
    float m_paddingL = 0.0f, m_paddingT = 0.0f;
    float m_paddingR = 0.0f, m_paddingB = 0.0f;
    bool m_fillWidth = true;          // 默认子元素宽度填充
    HAlign m_childAlign = HAlign::Left;
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
    
    // ✅ 重写setSize，在尺寸变化时触发布局
    UIHBox* setSize(float w, float h) {
        UINode::setSize(w, h);
        onLayout();  // 尺寸变化时重新布局
        return this;
    }

protected:
    void onLayout() override {
        float contentWidth = m_size.x - m_paddingL - m_paddingR;
        float contentHeight = m_size.y - m_paddingT - m_paddingB;
        
        // 计算所有可见子元素的总宽度
        float totalChildWidth = 0;
        int visibleCount = 0;
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            totalChildWidth += child->getSize().x;
            visibleCount++;
        }
        
        if (visibleCount == 0) return;
        
        // 计算起始位置和间距
        float x = m_paddingL;
        float spacing = m_spacing;
        
        switch (m_justify) {
            case Justify::Start:
                x = m_paddingL;
                break;
            case Justify::Center:
                x = m_paddingL + (contentWidth - totalChildWidth - spacing * (visibleCount - 1)) * 0.5f;
                break;
            case Justify::End:
                x = m_paddingL + (contentWidth - totalChildWidth - spacing * (visibleCount - 1));
                break;
            case Justify::SpaceBetween:
                x = m_paddingL;
                if (visibleCount > 1) {
                    spacing = (contentWidth - totalChildWidth) / (visibleCount - 1);
                }
                break;
            case Justify::SpaceAround:
                spacing = (contentWidth - totalChildWidth) / visibleCount;
                x = m_paddingL + spacing * 0.5f;
                break;
            case Justify::SpaceEvenly:
                spacing = (contentWidth - totalChildWidth) / (visibleCount + 1);
                x = m_paddingL + spacing;
                break;
        }
        
        // 布局子元素
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            
            auto childSize = child->getSize();
            float childWidth = childSize.x;
            float childHeight = m_fillHeight ? contentHeight : childSize.y;
            
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
            if (m_fillHeight) {
                child->setHeight(childHeight);
            }
            
            x += childWidth + spacing;
        }
    }

private:
    float m_spacing = 0.0f;
    float m_paddingL = 0.0f, m_paddingT = 0.0f;
    float m_paddingR = 0.0f, m_paddingB = 0.0f;
    bool m_fillHeight = false;        // 默认子元素高度不填充
    Justify m_justify = Justify::Start;
    VAlign m_childAlign = VAlign::Middle;
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
    
    // ✅ 重写setSize，在尺寸变化时触发布局
    UIGrid* setSize(float w, float h) {
        UINode::setSize(w, h);
        onLayout();  // 尺寸变化时重新布局
        return this;
    }

protected:
    void onLayout() override {
        float contentWidth = m_size.x - m_paddingL - m_paddingR;
        float contentHeight = m_size.y - m_paddingT - m_paddingB;
        
        // 计算列数
        int columns = m_columns;
        if (!m_fixedColumns && m_columnWidth > 0) {
            columns = static_cast<int>((contentWidth + m_spacingH) / (m_columnWidth + m_spacingH));
            columns = std::max(1, columns);
        }
        
        if (columns <= 0) return;
        
        // 计算单元格尺寸
        float cellWidth = (contentWidth - m_spacingH * (columns - 1)) / columns;
        
        // 布局子元素
        int index = 0;
        float y = m_paddingT;
        float maxRowHeight = 0;
        
        for (auto& child : m_children) {
            if (!child || !child->isVisible()) continue;
            
            int col = index % columns;
            int row = index / columns;
            
            // 新行
            if (col == 0 && index > 0) {
                y += maxRowHeight + m_spacingV;
                maxRowHeight = 0;
            }
            
            float x = m_paddingL + col * (cellWidth + m_spacingH);
            
            auto childSize = child->getSize();
            float childWidth = childSize.x;
            float childHeight = childSize.y;
            
            // 单元格内对齐
            float cellX = x;
            float cellY = y;
            
            switch (m_cellHAlign) {
                case HAlign::Left:
                    cellX = x;
                    break;
                case HAlign::Center:
                    cellX = x + (cellWidth - childWidth) * 0.5f;
                    break;
                case HAlign::Right:
                    cellX = x + (cellWidth - childWidth);
                    break;
            }
            
            child->setPosition(cellX, cellY);
            
            maxRowHeight = std::max(maxRowHeight, childHeight);
            index++;
        }
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
};

} // namespace Tina::UI
