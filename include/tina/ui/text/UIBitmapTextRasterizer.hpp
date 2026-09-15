#pragma once

#include <tina/ui/text/UITextRasterizer.hpp>
#include <tina/text/BitmapFont.hpp>

namespace Tina::UI {
// Built-in scalar/LTR bitmap adapter. Source is an immutable cooked CPU atlas;
// openFace({}, 0) opens that source. Outline shaping remains a separate adapter.
// Source images are never resampled: nearest filtering happens in the renderer.
[[nodiscard]] Core::Result<std::unique_ptr<IUITextRasterizer>> createBitmapTextRasterizer(
    std::shared_ptr<const Text::BitmapFontAtlas> source, UITextRasterizerCapacity capacity = {},
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
}
