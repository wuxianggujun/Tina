#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/text/BitmapFont.hpp>

namespace Tina::AssetFormat {

// Little endian: 32-byte header, 12-byte page records, 36-byte glyph records,
// 12-byte kerning records. Page order equals required Texture2D dependency order.
namespace BitmapFontWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::usize HeaderBytes = 32;
inline constexpr Core::usize PageBytes = 12;
inline constexpr Core::usize GlyphBytes = 36;
inline constexpr Core::usize KerningBytes = 12;
}
[[nodiscard]] Core::Result<std::vector<std::byte>> writeBitmapFontPayloadBytes(const Text::BitmapFont& font);
[[nodiscard]] Core::Result<Text::BitmapFont> parseBitmapFontPayload(std::span<const std::byte> payload);
[[nodiscard]] Core::Status validateBitmapFontTexturePage(const Text::BitmapFontPage& page, const Texture2DPayloadView& texture);
// Like all cooked dependency tables, textureIds must be strictly increasing.
[[nodiscard]] Core::Result<std::vector<std::byte>> writeCookedBitmapFontAsset(
    Core::AssetId id, const Text::BitmapFont& font, std::span<const Core::AssetId> textureIds,
    TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
