#pragma once

// Public factory for the optional FreeType adapter. The header stays
// FreeType-free; the implementation lives in tina_ui_freetype and is only
// linked when TINA_BUILD_UI_FREETYPE=ON.

#include <tina/ui/text/UITextRasterizer.hpp>

namespace Tina::UI {

// Creates a FreeType-backed rasterizer. openFace requires non-empty font bytes
// (complete face blob). Public headers and Null graphs never include FreeType.
[[nodiscard]] Core::Result<std::unique_ptr<IUITextRasterizer>> createFreeTypeTextRasterizer(
    UITextRasterizerCapacity capacity = {},
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

} // namespace Tina::UI
