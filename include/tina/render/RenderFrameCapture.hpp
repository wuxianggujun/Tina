#pragma once

#include <tina/core/base/Types.hpp>

#include <vector>

namespace Tina::Render {

// Owner-thread owned RGBA8 top-left origin frame capture.
// M11-D1: optional product pixel evidence; backends may return Unsupported.
struct Rgba8FrameCapture final {
    u32 width = 0;
    u32 height = 0;
    // Row-major RGBA8, size == width * height * 4.
    std::vector<std::byte> rgba8Pixels{};

    [[nodiscard]] bool empty() const noexcept
    {
        return width == 0 || height == 0 || rgba8Pixels.empty();
    }

    [[nodiscard]] usize byteCount() const noexcept
    {
        return rgba8Pixels.size();
    }
};

} // namespace Tina::Render
