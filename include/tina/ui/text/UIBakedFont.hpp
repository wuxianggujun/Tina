#pragma once

#include <tina/ui/text/UITextRasterizer.hpp>

namespace Tina::UI {

inline constexpr u32 UIBakedFontSchemaVersion = 1;
inline constexpr u32 UIBakedFontHeaderBytes = 48;
inline constexpr u32 UIBakedGlyphRecordBytes = 44;

struct UIBakedGlyph final {
    u32 glyphIndex = 0;
    u32 width = 0;
    u32 height = 0;
    UIGlyphDevicePixelSize rasterSize{};
    UIGlyphImageKind imageKind = UIGlyphImageKind::Msdf;
    u32 pixelOffset = 0;
    u32 pixelBytes = 0;
    float bearingXEm = 0.0F;
    float bearingYEm = 0.0F;
    float distanceRange = 0.0F;
};

struct UIBakedFontView final {
    u64 fontFingerprint = 0;
    u32 faceIndex = 0;
    u32 glyphCount = 0;
    std::span<const std::byte> records{};
    std::span<const u8> pixels{};
    // Only indices < glyphCount are valid; an invalid index returns an empty record.
    [[nodiscard]] UIBakedGlyph glyph(u32 index) const noexcept;
};

// Non-cryptographic source identity for stale-asset detection, not a signature.
[[nodiscard]] u64 uiFontFingerprint(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] Core::Result<UIBakedFontView> parseUIBakedFont(std::span<const std::byte> bytes);

} // namespace Tina::UI
