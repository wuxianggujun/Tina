#include <tina/ui/text/UIBakedFont.hpp>
#include <tina/ui/UIErrors.hpp>

#include <bit>
#include <cmath>
#include <cstring>

namespace Tina::UI {
namespace {
u32 readU32(std::span<const std::byte> bytes, usize offset) noexcept
{
    u32 value = 0;
    for (u32 index = 0; index < 4; ++index) { value |= std::to_integer<u32>(bytes[offset + index]) << (index * 8U); }
    return value;
}
}

u64 uiFontFingerprint(std::span<const std::byte> bytes) noexcept
{
    u64 hash = 14695981039346656037ULL;
    for (std::byte byte : bytes) { hash = (hash ^ std::to_integer<u8>(byte)) * 1099511628211ULL; }
    return hash;
}

UIBakedGlyph UIBakedFontView::glyph(u32 index) const noexcept
{
    if (index >= glyphCount || static_cast<u64>(index + 1U) * UIBakedGlyphRecordBytes > records.size()) { return {}; }
    const auto record = records.subspan(static_cast<usize>(index) * UIBakedGlyphRecordBytes, UIBakedGlyphRecordBytes);
    return {readU32(record, 0), readU32(record, 4), readU32(record, 8),
            {readU32(record, 12), readU32(record, 16)},
            static_cast<UIGlyphImageKind>(readU32(record, 20)),
            readU32(record, 24), readU32(record, 28),
            std::bit_cast<float>(readU32(record, 32)), std::bit_cast<float>(readU32(record, 36)),
            std::bit_cast<float>(readU32(record, 40))};
}

Core::Result<UIBakedFontView> parseUIBakedFont(std::span<const std::byte> bytes)
{
    const auto invalid = [] { return Core::failure(UIErrorCode::InvalidFont, "Invalid or incompatible cooked MSDF font seed"); };
    if (bytes.size() < UIBakedFontHeaderBytes || std::memcmp(bytes.data(), "TMSDFONT", 8) != 0 ||
        readU32(bytes, 8) != UIBakedFontSchemaVersion || readU32(bytes, 12) != UIBakedFontHeaderBytes ||
        readU32(bytes, 36) != UIBakedGlyphRecordBytes || readU32(bytes, 40) != UITextMsdfPixelsPerEm ||
        std::bit_cast<float>(readU32(bytes, 44)) != UITextMsdfDistanceRange) { return invalid(); }
    const u32 count = readU32(bytes, 28);
    const u32 pixelBytes = readU32(bytes, 32);
    const u64 recordBytes = static_cast<u64>(count) * UIBakedGlyphRecordBytes;
    if (count > UITextRasterizerCapacity::MaxGlyphsPerRaster ||
        pixelBytes > UITextRasterizerCapacity::MaxCoverageByteCapacity ||
        UIBakedFontHeaderBytes + recordBytes + pixelBytes != bytes.size()) { return invalid(); }
    UIBakedFontView view{
        static_cast<u64>(readU32(bytes, 16)) | (static_cast<u64>(readU32(bytes, 20)) << 32U),
        readU32(bytes, 24), count, bytes.subspan(UIBakedFontHeaderBytes, static_cast<usize>(recordBytes)),
        {reinterpret_cast<const u8*>(bytes.data() + UIBakedFontHeaderBytes + recordBytes), pixelBytes}};
    for (u32 index = 0; index < count; ++index)
    {
        const auto record = view.records.subspan(static_cast<usize>(index) * UIBakedGlyphRecordBytes);
        const UIBakedGlyph glyph = view.glyph(index);
        if (readU32(record, 20) < static_cast<u32>(UIGlyphImageKind::Msdf) ||
            readU32(record, 20) > static_cast<u32>(UIGlyphImageKind::Color) ||
            glyph.width > 512 || glyph.height > 512 || glyph.rasterSize.x == 0 || glyph.rasterSize.y == 0 ||
            glyph.rasterSize.x > 4096 || glyph.rasterSize.y > 4096 ||
            static_cast<u64>(glyph.width) * glyph.height * 4U != glyph.pixelBytes ||
            glyph.pixelOffset > view.pixels.size() || glyph.pixelBytes > view.pixels.size() - glyph.pixelOffset ||
            !std::isfinite(glyph.bearingXEm) || !std::isfinite(glyph.bearingYEm) ||
            std::abs(glyph.bearingXEm) > 16.0F || std::abs(glyph.bearingYEm) > 16.0F ||
            (glyph.imageKind == UIGlyphImageKind::Msdf &&
             (glyph.rasterSize != UIGlyphDevicePixelSize{UITextMsdfPixelsPerEm, UITextMsdfPixelsPerEm} ||
              glyph.distanceRange != UITextMsdfDistanceRange)) ||
            (glyph.imageKind == UIGlyphImageKind::Color && glyph.distanceRange != 0.0F)) { return invalid(); }
    }
    return view;
}
} // namespace Tina::UI
