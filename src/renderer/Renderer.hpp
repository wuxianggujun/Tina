//
// 最薄渲染器封装：基于 bgfx 的视图/编码器
// - 负责每帧 begin/end（最终调用 bgfx::frame）
// - 提供设置视图、统计与获取编码器的接口

#pragma once

#include <bgfx/bgfx.h>
#include "../core/Core.hpp"
#include "../core/Math.hpp"

namespace Tina::Gfx {

struct ViewDesc {
    bgfx::ViewId id = 0;
    Tina::Math::IVec2 size{0, 0};
    bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
    uint16_t x = 0, y = 0, w = 0, h = 0;
    uint16_t clear_flags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH;
    uint32_t clear_rgba = 0x303030ff;
    float clear_depth = 1.0f;
};

struct FrameStats {
    Tina::u32 drawcalls = 0;
    Tina::u64 upload_bytes = 0;
};

class Renderer {
public:
    Renderer() = default;

    void setDisplaySize(int w, int h) { m_display = { w, h }; }
    const Tina::Math::IVec2& getDisplaySize() const { return m_display; }

    void beginFrame() { m_stats = {}; }
    void endFrame() { bgfx::frame(); }

    void setupView(const ViewDesc& v) {
        bgfx::setViewRect(v.id, v.x, v.y, v.w ? v.w : (uint16_t)m_display.x, v.h ? v.h : (uint16_t)m_display.y);
        if (bgfx::isValid(v.framebuffer)) bgfx::setViewFrameBuffer(v.id, v.framebuffer);
        bgfx::setViewClear(v.id, v.clear_flags, v.clear_rgba, v.clear_depth);
    }

    bgfx::Encoder* getEncoder() { return bgfx::begin(false); }
    void submit(bgfx::Encoder* enc, bgfx::ViewId view, bgfx::ProgramHandle prog, Tina::u64 state) {
        if (enc) {
            enc->setState(state);
            enc->submit(view, prog);
            ++m_stats.drawcalls;
        }
    }
    FrameStats stats() const { return m_stats; }

private:
    Tina::Math::IVec2 m_display{ 1280, 720 };
    FrameStats m_stats{};
};

} // namespace Tina::Gfx

