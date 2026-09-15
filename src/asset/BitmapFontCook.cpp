#include "BitmapFontCook.hpp"
#include "stb_image.h"
#include <tina/asset/AssetErrors.hpp>
#include <tina/asset_format/BitmapFontPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/serialization/JsonArchive.hpp>

#include <array>
#include <algorithm>
#include <numeric>
#include <memory>
#include <set>

namespace Tina::Asset::Detail {
namespace {
bool validPagePath(std::string_view path) noexcept
{
    if (path.empty() || path.front() == '/' || path.front() == '\\' ||
        path.find('\0') != std::string_view::npos || path.find(':') != std::string_view::npos) return false;
    while (!path.empty()) {
        const auto separator = path.find_first_of("/\\");
        if (path.substr(0, separator) == "..") return false;
        if (separator == std::string_view::npos) break;
        path.remove_prefix(separator + 1);
    }
    return true;
}
constexpr std::array PngSignature{std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
    std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
}

Core::Result<std::vector<CatalogCookAssetSpec>> cookBitmapFontSource(Core::AssetId fontId,
    std::span<const std::byte> source,
    const std::function<Core::Result<std::vector<std::byte>>(std::string_view)>& readPage)
try {
    if (!fontId || !readPage)
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "Bitmap font source requires an ID and page reader");
    auto document = Core::JsonDocument::parse(source, {.maxInputBytes = 16 * 1024 * 1024, .maxDepth = 8, .maxNodes = 1'000'000});
    if (!document) return Core::failure(document.error());
    Serialization::Reader root{document->root()};
    if (auto status = root.fieldsOnly({"schema", "nominalSize", "lineHeight", "baseline", "fallback", "pages", "glyphs", "kerning"}); !status)
        return Core::failure(status.error());
    auto schema = root.field<Core::u32>("schema");
    auto nominalSize = root.field<float>("nominalSize"), lineHeight = root.field<float>("lineHeight"), baseline = root.field<float>("baseline");
    auto fallback = root.field<Core::u32>("fallback");
    auto pages = root.member("pages"), glyphs = root.member("glyphs"), kerning = root.member("kerning");
    if (!schema || *schema != 1 || !nominalSize || !lineHeight || !baseline || !fallback || !pages || !glyphs || !kerning)
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "Bitmap font source requires current schema and complete metrics/tables");
    if (!pages->value().isArray() || pages->size() == 0 || pages->size() > Text::BitmapFont::MaxPages ||
        !glyphs->value().isArray() || glyphs->size() > Text::BitmapFont::MaxGlyphs ||
        !kerning->value().isArray() || kerning->size() > Text::BitmapFont::MaxKerningPairs)
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "Bitmap font source table limit exceeded");
    Text::BitmapFontDesc descriptor{.nominalSize = *nominalSize, .lineHeight = *lineHeight, .baseline = *baseline, .fallbackCodepoint = *fallback};
    std::vector<CatalogCookAssetSpec> assets;
    std::vector<AssetFormat::CookedAssetWriteDependency> dependencies;
    std::set<Core::AssetId> identities{fontId};
    Core::u64 totalPixelBytes = 0;
    for (Core::usize index = 0; index < pages->size(); ++index) {
        auto page = pages->element(index);
        if (!page) return Core::failure(page.error());
        if (auto status = page->fieldsOnly({"textureId", "image", "kind"}); !status) return Core::failure(status.error());
        auto idText = page->field<std::string>("textureId"), image = page->field<std::string>("image"), kind = page->field<std::string>("kind");
        if (!idText || !image || !kind || !validPagePath(*image) || (*kind != "coverage" && *kind != "color"))
            return Core::failure(page->error(AssetErrorCode::InvalidCatalogConfig, "Invalid bitmap page source"));
        auto id = Core::AssetId::parseCanonical(*idText);
        if (!id || !*id || !identities.insert(*id).second)
            return Core::failure(page->error(AssetErrorCode::InvalidCatalogConfig, "Invalid or duplicate bitmap page ID"));
        auto imageBytes = readPage(*image);
        if (!imageBytes) return Core::failure(imageBytes.error());
        if (imageBytes->size() < PngSignature.size() || imageBytes->size() > Text::BitmapFontAtlas::MaxPixelBytes)
            return Core::failure(page->error(AssetErrorCode::InvalidCatalogConfig, "Bitmap page source exceeds byte limit"));
        if (!std::equal(PngSignature.begin(), PngSignature.end(), imageBytes->begin()))
            return Core::failure(page->error(AssetErrorCode::InvalidCatalogConfig, "Bitmap page source must be PNG"));
        int width = 0, height = 0, channels = 0;
        const auto* bytes = reinterpret_cast<const stbi_uc*>(imageBytes->data());
        if (!stbi_info_from_memory(bytes, static_cast<int>(imageBytes->size()), &width, &height, &channels) ||
            width <= 0 || height <= 0 || width > static_cast<int>(Text::BitmapFont::MaxPageDimension) || height > static_cast<int>(Text::BitmapFont::MaxPageDimension))
            return Core::failure(page->error(AssetErrorCode::InvalidCatalogConfig, "Invalid bitmap page image dimensions"));
        const Core::u64 pixelBytes = static_cast<Core::u64>(width) * static_cast<Core::u64>(height) * 4;
        if (pixelBytes > Text::BitmapFontAtlas::MaxPixelBytes - totalPixelBytes)
            return Core::failure(page->error(AssetErrorCode::InvalidCatalogConfig, "Bitmap font decoded pages exceed 64 MiB"));
        totalPixelBytes += pixelBytes;
        const int expectedWidth = width, expectedHeight = height;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
            stbi_load_from_memory(bytes, static_cast<int>(imageBytes->size()), &width, &height, &channels, 4), stbi_image_free};
        if (!pixels || width != expectedWidth || height != expectedHeight)
            return Core::failure(page->error(AssetErrorCode::InvalidCatalogConfig, "Bitmap page image decoding failed or changed dimensions"));
        const std::array levels{AssetFormat::Texture2DLevelDesc{static_cast<Core::u16>(width), static_cast<Core::u16>(height),
            {reinterpret_cast<const std::byte*>(pixels.get()), static_cast<Core::usize>(pixelBytes)}}};
        auto payload = AssetFormat::writeTexture2DPayloadBytes({
            .pixelFormat = AssetFormat::Texture2DPixelFormat::Rgba8Unorm,
            .colorSpace = AssetFormat::Texture2DColorSpace::Srgb,
            .sampler = {.minFilter = AssetFormat::Texture2DFilterMode::Point, .magFilter = AssetFormat::Texture2DFilterMode::Point,
                        .mipFilter = AssetFormat::Texture2DMipFilterMode::None}, .levels = levels});
        if (!payload) return Core::failure(payload.error());
        assets.push_back({.assetKind = AssetFormat::AssetKind::Texture2D, .assetId = *id,
            .assetTypeVersion = AssetFormat::Texture2DWire::SchemaVersion, .payload = std::move(*payload)});
        descriptor.pages.push_back({static_cast<Core::u32>(width), static_cast<Core::u32>(height),
            *kind == "coverage" ? Text::BitmapFontImageKind::Coverage : Text::BitmapFontImageKind::Color});
        dependencies.push_back({*id, AssetFormat::AssetKind::Texture2D, AssetFormat::DependencyFlags::Required});
    }
    // Compact integer rect + floating metrics tuples avoid a second naming/API layer.
    for (Core::usize index = 0; index < glyphs->size(); ++index) {
        auto item = glyphs->element(index);
        if (!item) return Core::failure(item.error());
        if (auto status = item->fieldsOnly({"codepoint", "page", "rect", "advance", "bearing"}); !status) return Core::failure(status.error());
        auto codepoint = item->field<Core::u32>("codepoint"), page = item->field<Core::u32>("page");
        auto rect = item->field<std::array<Core::u32, 4>>("rect");
        auto advance = item->field<float>("advance");
        auto bearing = item->field<std::array<float, 2>>("bearing");
        if (!codepoint || !page || !rect || !advance || !bearing)
            return Core::failure(item->error(AssetErrorCode::InvalidCatalogConfig, "Invalid bitmap glyph source"));
        descriptor.glyphs.push_back({*codepoint, *page, (*rect)[0], (*rect)[1], (*rect)[2], (*rect)[3], *advance, (*bearing)[0], (*bearing)[1]});
    }
    for (Core::usize index = 0; index < kerning->size(); ++index) {
        auto item = kerning->element(index);
        if (!item) return Core::failure(item.error());
        if (auto status = item->fieldsOnly({"left", "right", "adjustment"}); !status) return Core::failure(status.error());
        auto left = item->field<Core::u32>("left"), right = item->field<Core::u32>("right");
        auto adjustment = item->field<float>("adjustment");
        if (!left || !right || !adjustment) return Core::failure(item->error(AssetErrorCode::InvalidCatalogConfig, "Invalid kerning source"));
        descriptor.kerning.push_back({*left, *right, *adjustment});
    }
    // Cooked dependency tables are sorted by AssetId. Preserve authored glyph
    // page semantics while normalizing page records into that canonical order.
    std::vector<Core::usize> order(descriptor.pages.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto left, auto right) { return dependencies[left].assetId < dependencies[right].assetId; });
    auto originalPages = descriptor.pages;
    auto originalDependencies = dependencies;
    std::array<Core::u32, Text::BitmapFont::MaxPages> remap{};
    for (Core::usize index = 0; index < order.size(); ++index) {
        descriptor.pages[index] = originalPages[order[index]];
        dependencies[index] = originalDependencies[order[index]];
        remap[order[index]] = static_cast<Core::u32>(index);
    }
    for (auto& glyph : descriptor.glyphs) {
        if (glyph.page >= order.size()) return Core::failure(AssetErrorCode::InvalidCatalogConfig, "Bitmap glyph page index out of range");
        glyph.page = remap[glyph.page];
    }
    auto font = Text::BitmapFont::Create(std::move(descriptor));
    if (!font) return Core::failure(font.error());
    auto payload = AssetFormat::writeBitmapFontPayloadBytes(*font);
    if (!payload) return Core::failure(payload.error());
    assets.push_back({.assetKind = AssetFormat::AssetKind::Font, .assetId = fontId,
        .assetTypeVersion = AssetFormat::BitmapFontWire::SchemaVersion, .payload = std::move(*payload), .dependencies = std::move(dependencies)});
    return assets;
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap font cook allocation failed"); }
}
