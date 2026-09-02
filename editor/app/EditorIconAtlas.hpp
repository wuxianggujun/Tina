#pragma once

#include "EditorIconAtlas.generated.hpp"

#include <tina/core/id/AssetId.hpp>
#include <tina/ui/UIIcon.hpp>

#include <cstddef>

namespace Tina::EditorApp::WorkspaceInternal {

[[nodiscard]] inline constexpr Core::AssetId editorIconAtlasAssetId() noexcept
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x54};
    bytes[1] = std::byte{0x49};
    bytes[2] = std::byte{0x4E};
    bytes[3] = std::byte{0x41};
    bytes[4] = std::byte{0x45};
    bytes[5] = std::byte{0x44};
    bytes[6] = std::byte{0x49};
    bytes[7] = std::byte{0x54};
    bytes[8] = std::byte{0x4F};
    bytes[9] = std::byte{0x52};
    bytes[15] = std::byte{0x04};
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] inline constexpr UI::UIIconContent
editorIconContent(EditorIcon icon) noexcept
{
    const EditorIconAtlasRect& source =
        EditorIconAtlasRects[static_cast<Core::u32>(icon)];
    return UI::UIIconContent{
        .source = {
            .texture = editorIconAtlasAssetId(),
            .sourcePixels = {
                .x = source.x,
                .y = source.y,
                .width = source.width,
                .height = source.height,
            },
            .texturePixelExtent = {
                .width = EditorIconAtlasWidth,
                .height = EditorIconAtlasHeight,
            },
            .intrinsicLogicalSize = {
                .width = static_cast<float>(EditorIconLogicalExtent),
                .height = static_cast<float>(EditorIconLogicalExtent),
            },
        },
        .sampling = UI::UIImageSampling::Linear,
    };
}

} // namespace Tina::EditorApp::WorkspaceInternal
