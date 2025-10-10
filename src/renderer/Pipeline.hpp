//
// 简易 Pipeline：分桶/排序/提交
// - 视图布局：0 Opaque, 1 Transparent, 3 UI（建议统一使用 UIConstants 中的定义）
// - 提供添加绘制项接口（当前为占位，实际项目可替换为网格/材质）

#pragma once

#include <bgfx/bgfx.h>
#include "Renderer.hpp"
#include "SortKey.hpp"
#include "../core/Math.hpp"
#include "../core/Container.hpp"
#include <algorithm>
#include <bx/math.h>

namespace Tina::Gfx {

enum class Bucket : Tina::u8 { Opaque = 0, Transparent = 1, UI = 2 };

struct DrawItem {
    Tina::u64 sort_key = 0;
    Bucket bucket = Bucket::Opaque;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    Tina::u64 state = 0; // bgfx state
    // 真实项目应包含 vb/ib/uniform/samplers 等
};

class Pipeline {
public:
    explicit Pipeline(Renderer& r) : m_renderer(r) {}

    void setViewport(int w, int h) { m_viewport = { w, h }; }

    void begin() {
        // 配置视图
        ViewDesc v0{ 0, m_viewport, BGFX_INVALID_HANDLE, 0, 0, (uint16_t)m_viewport.x, (uint16_t)m_viewport.y,
                     (uint16_t)(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH), 0x303030ff, 1.0f };
        ViewDesc v1 = v0; v1.id = 1; v1.clear_flags = 0; // 透明不清屏
        ViewDesc v2 = v0; v2.id = 3; v2.clear_flags = BGFX_CLEAR_DEPTH; // UI 仅清深度
        m_renderer.setupView(v0);
        m_renderer.setupView(v1);
        m_renderer.setupView(v2);

        // 统一设置 UI 视图(3)的正交投影（像素坐标，左上为原点）
        {
            float ortho[16];
            const bgfx::Caps* caps = bgfx::getCaps();
            bx::mtxOrtho(ortho,
                         0.0f, (float)m_viewport.x,
                         (float)m_viewport.y, 0.0f,
                         -1.0f, 1.0f, 0.0f,
                         caps->homogeneousDepth);
            bgfx::setViewTransform(3, nullptr, ortho);
        }
        m_items.clear();
    }

    void addDrawItem(const DrawItem& di) { m_items.push_back(di); }

    void submit() {
        // 按 sort_key 排序
        std::sort(m_items.begin(), m_items.end(), [](const DrawItem& a, const DrawItem& b){ return a.sort_key < b.sort_key; });
        // 演示：这里只做 touch，防止提交空视图导致上一帧残影
        bgfx::touch(0);
        bgfx::touch(1);
        bgfx::touch(3);
        // 将来在此处获取 Encoder 并提交 drawcall
    }

private:
    Renderer& m_renderer;
    Tina::Math::IVec2 m_viewport{1280, 720};
    Tina::Container::Vector<DrawItem> m_items;
};

} // namespace Tina::Gfx
