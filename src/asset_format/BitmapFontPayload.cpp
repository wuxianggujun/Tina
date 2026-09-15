#include <tina/asset_format/BitmapFontPayload.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>

#include <bit>
#include <set>

namespace Tina::AssetFormat {
namespace {
void appendU32(std::vector<std::byte>& bytes, Core::u32 value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
}
void appendFloat(std::vector<std::byte>& bytes, float value) { appendU32(bytes, std::bit_cast<Core::u32>(value)); }
Core::u32 readU32(std::span<const std::byte> bytes, Core::usize& offset) noexcept {
    Core::u32 value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= std::to_integer<Core::u32>(bytes[offset++]) << shift;
    return value;
}
float readFloat(std::span<const std::byte> bytes, Core::usize& offset) noexcept { return std::bit_cast<float>(readU32(bytes, offset)); }
Core::usize payloadSize(Core::usize pages, Core::usize glyphs, Core::usize pairs) noexcept {
    return BitmapFontWire::HeaderBytes + pages * BitmapFontWire::PageBytes +
        glyphs * BitmapFontWire::GlyphBytes + pairs * BitmapFontWire::KerningBytes;
}
}

Core::Result<std::vector<std::byte>> writeBitmapFontPayloadBytes(const Text::BitmapFont& font)
try {
    const auto& descriptor = font.descriptor();
    std::vector<std::byte> bytes;
    bytes.reserve(payloadSize(descriptor.pages.size(), descriptor.glyphs.size(), descriptor.kerning.size()));
    appendU32(bytes, BitmapFontWire::SchemaVersion);
    appendU32(bytes, static_cast<Core::u32>(descriptor.pages.size()));
    appendU32(bytes, static_cast<Core::u32>(descriptor.glyphs.size()));
    appendU32(bytes, static_cast<Core::u32>(descriptor.kerning.size()));
    appendFloat(bytes, descriptor.nominalSize); appendFloat(bytes, descriptor.lineHeight); appendFloat(bytes, descriptor.baseline);
    appendU32(bytes, descriptor.fallbackCodepoint);
    for (const auto& page : descriptor.pages) {
        appendU32(bytes, page.width); appendU32(bytes, page.height); appendU32(bytes, static_cast<Core::u32>(page.imageKind));
    }
    for (const auto& glyph : descriptor.glyphs) {
        appendU32(bytes, glyph.codepoint); appendU32(bytes, glyph.page);
        appendU32(bytes, glyph.x); appendU32(bytes, glyph.y); appendU32(bytes, glyph.width); appendU32(bytes, glyph.height);
        appendFloat(bytes, glyph.advance); appendFloat(bytes, glyph.bearingX); appendFloat(bytes, glyph.bearingY);
    }
    for (const auto& pair : descriptor.kerning) {
        appendU32(bytes, pair.left); appendU32(bytes, pair.right); appendFloat(bytes, pair.adjustment);
    }
    return bytes;
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap font encoding allocation failed"); }

Core::Result<Text::BitmapFont> parseBitmapFontPayload(std::span<const std::byte> payload)
try {
    if (payload.size() < BitmapFontWire::HeaderBytes)
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "Truncated bitmap font header");
    Core::usize offset = 0;
    if (readU32(payload, offset) != BitmapFontWire::SchemaVersion)
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "Unsupported bitmap font schema");
    const auto pages = readU32(payload, offset), glyphs = readU32(payload, offset), pairs = readU32(payload, offset);
    if (pages > Text::BitmapFont::MaxPages || glyphs > Text::BitmapFont::MaxGlyphs || pairs > Text::BitmapFont::MaxKerningPairs ||
        payload.size() != payloadSize(pages, glyphs, pairs))
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "Bitmap font count or length mismatch");
    Text::BitmapFontDesc descriptor;
    descriptor.nominalSize = readFloat(payload, offset); descriptor.lineHeight = readFloat(payload, offset);
    descriptor.baseline = readFloat(payload, offset); descriptor.fallbackCodepoint = readU32(payload, offset);
    descriptor.pages.reserve(pages); descriptor.glyphs.reserve(glyphs); descriptor.kerning.reserve(pairs);
    for (Core::u32 index = 0; index < pages; ++index) {
        const auto width = readU32(payload, offset), height = readU32(payload, offset), kind = readU32(payload, offset);
        if (kind > static_cast<Core::u32>(Text::BitmapFontImageKind::Color))
            return Core::failure(AssetFormatErrorCode::UnsupportedValue, "Unknown bitmap page image kind");
        descriptor.pages.push_back({width, height, static_cast<Text::BitmapFontImageKind>(kind)});
    }
    for (Core::u32 index = 0; index < glyphs; ++index) {
        Text::BitmapGlyph glyph;
        glyph.codepoint = readU32(payload, offset); glyph.page = readU32(payload, offset);
        glyph.x = readU32(payload, offset); glyph.y = readU32(payload, offset);
        glyph.width = readU32(payload, offset); glyph.height = readU32(payload, offset);
        glyph.advance = readFloat(payload, offset); glyph.bearingX = readFloat(payload, offset); glyph.bearingY = readFloat(payload, offset);
        descriptor.glyphs.push_back(glyph);
    }
    for (Core::u32 index = 0; index < pairs; ++index) {
        Text::BitmapKerning pair;
        pair.left = readU32(payload, offset); pair.right = readU32(payload, offset); pair.adjustment = readFloat(payload, offset);
        descriptor.kerning.push_back(pair);
    }
    return Text::BitmapFont::Create(std::move(descriptor));
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap font decoding allocation failed"); }

Core::Result<std::vector<std::byte>> writeCookedBitmapFontAsset(Core::AssetId id, const Text::BitmapFont& font,
    std::span<const Core::AssetId> textureIds, TargetPlatform platform)
try {
    if (!id || textureIds.size() != font.descriptor().pages.size())
        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "Bitmap font page dependency count mismatch");
    std::vector<CookedAssetWriteDependency> dependencies;
    std::set<Core::AssetId> unique;
    for (const auto texture : textureIds) {
        if (!texture || texture == id || !unique.insert(texture).second ||
            (!dependencies.empty() && !(dependencies.back().assetId < texture)))
            return Core::failure(AssetFormatErrorCode::InvalidIdentity, "Invalid or duplicate bitmap page dependency");
        dependencies.push_back({texture, AssetKind::Texture2D, DependencyFlags::Required});
    }
    auto payload = writeBitmapFontPayloadBytes(font);
    if (!payload) return Core::failure(payload.error());
    return writeCookedAssetBytes({.assetKind = AssetKind::Font, .assetTypeVersion = BitmapFontWire::SchemaVersion,
        .targetPlatform = platform, .assetId = id, .dependencies = dependencies, .payload = *payload,
        .payloadAlignment = 16, .computeContentHash = true});
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap font dependency allocation failed"); }

Core::Status validateBitmapFontTexturePage(const Text::BitmapFontPage& page, const Texture2DPayloadView& texture) {
    if (texture.width != page.width || texture.height != page.height || texture.levelCount != 1 ||
        texture.pixelFormat != Texture2DPixelFormat::Rgba8Unorm || texture.colorSpace != Texture2DColorSpace::Srgb ||
        texture.sampler.minFilter != Texture2DFilterMode::Point || texture.sampler.magFilter != Texture2DFilterMode::Point ||
        texture.sampler.wrapU != Texture2DWrapMode::Clamp || texture.sampler.wrapV != Texture2DWrapMode::Clamp ||
        texture.sampler.mipFilter != Texture2DMipFilterMode::None)
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "Bitmap font requires matching point-clamp single-level sRGBA8 pages");
    return Core::success();
}

} // namespace Tina::AssetFormat
