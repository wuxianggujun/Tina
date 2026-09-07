#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>
#include <tina/ui/text/UIBakedFont.hpp>
#include <tina/ui/text/UIGlyphAtlas.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/text/Utf8.hpp>

#include <png.h>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
using namespace Tina;

void appendU32(std::vector<std::byte>& output, u32 value)
{
    for (u32 byte = 0; byte < 4; ++byte) { output.push_back(static_cast<std::byte>((value >> (8U * byte)) & 255U)); }
}

Core::Status bake(std::string_view fontPath, std::string_view stringsPath, std::string_view outputStem)
{
    auto font = Core::readFile(fontPath, {.maxBytes = 64U * 1024U * 1024U, .memoryResource = std::pmr::get_default_resource()});
    if (!font) { return Core::failure(font.error()); }
    auto strings = Core::readFile(stringsPath, {.maxBytes = 64U * 1024U, .memoryResource = std::pmr::get_default_resource()});
    if (!strings) { return Core::failure(strings.error()); }
    const std::string_view text(reinterpret_cast<const char*>(strings->data()), strings->size());
    auto rasterizer = UI::createFreeTypeTextRasterizer();
    if (!rasterizer) { return Core::failure(rasterizer.error()); }
    auto face = (*rasterizer)->openFace(*font);
    if (!face) { return Core::failure(face.error()); }
    constexpr float em = UI::UITextMsdfPixelsPerEm;
    auto batch = (*rasterizer)->raster(*face, text, {.logicalSize = em});
    if (!batch) { return Core::failure(batch.error()); }
    auto atlas = UI::UIGlyphAtlas::Create();
    if (!atlas) { return Core::failure(atlas.error()); }
    struct Entry final { UI::UIBakedGlyph glyph; UI::UIGlyphPlacement placement; float advance; };
    std::vector<Entry> entries;
    std::vector<std::byte> pixels;
    std::set<UI::UIGlyphKey> seen;
    for (const auto& glyph : batch->glyphs)
    {
        const UI::UIGlyphKey key{glyph.face, glyph.glyphIndex, glyph.rasterSize, glyph.imageKind};
        if (!seen.insert(key).second) { continue; }
        const usize byteCount = static_cast<usize>(glyph.width) * glyph.height * 4U;
        const auto image = batch->coverage.subspan(glyph.coverageOffset, byteCount);
        auto placement = (*atlas)->insert(key, glyph, image);
        if (!placement) { return Core::failure(placement.error()); }
        entries.push_back({{glyph.glyphIndex, glyph.width, glyph.height, glyph.rasterSize, glyph.imageKind,
            static_cast<u32>(pixels.size()), static_cast<u32>(byteCount),
            glyph.bearingX / em, glyph.bearingY / em, glyph.distanceRange}, *placement, glyph.nominalAdvance / em});
        for (u8 byte : image) { pixels.push_back(static_cast<std::byte>(byte)); }
    }
    const u64 fingerprint = UI::uiFontFingerprint(*font);
    std::vector<std::byte> cooked;
    for (char byte : std::string_view("TMSDFONT")) { cooked.push_back(static_cast<std::byte>(byte)); }
    appendU32(cooked, UI::UIBakedFontSchemaVersion);
    appendU32(cooked, UI::UIBakedFontHeaderBytes);
    appendU32(cooked, static_cast<u32>(fingerprint));
    appendU32(cooked, static_cast<u32>(fingerprint >> 32U));
    appendU32(cooked, 0); // explicit face index for this TTF/OTF cook
    appendU32(cooked, static_cast<u32>(entries.size()));
    appendU32(cooked, static_cast<u32>(pixels.size()));
    appendU32(cooked, UI::UIBakedGlyphRecordBytes);
    appendU32(cooked, UI::UITextMsdfPixelsPerEm);
    appendU32(cooked, std::bit_cast<u32>(UI::UITextMsdfDistanceRange));
    for (const auto& entry : entries)
    {
        const auto& glyph = entry.glyph;
        for (u32 value : {glyph.glyphIndex, glyph.width, glyph.height, glyph.rasterSize.x, glyph.rasterSize.y,
                           static_cast<u32>(glyph.imageKind), glyph.pixelOffset, glyph.pixelBytes,
                           std::bit_cast<u32>(glyph.bearingXEm), std::bit_cast<u32>(glyph.bearingYEm),
                           std::bit_cast<u32>(glyph.distanceRange)}) { appendU32(cooked, value); }
    }
    cooked.insert(cooked.end(), pixels.begin(), pixels.end());
    auto validated = UI::parseUIBakedFont(cooked);
    if (!validated) { return Core::failure(validated.error()); }

    const auto capacity = (*atlas)->capacity();
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = capacity.width;
    image.height = capacity.height;
    image.format = PNG_FORMAT_RGBA;
    png_alloc_size_t pngSize = 0;
    if (!png_image_write_to_memory(&image, nullptr, &pngSize, 0, (*atlas)->pagePixels().data(), 0, nullptr))
    {
        const std::string message = image.message;
        png_image_free(&image);
        return Core::failure(Core::CoreErrorCode::Internal, message);
    }
    std::vector<std::byte> png(pngSize);
    if (!png_image_write_to_memory(&image, png.data(), &pngSize, 0, (*atlas)->pagePixels().data(), 0, nullptr))
    {
        const std::string message = image.message;
        png_image_free(&image);
        return Core::failure(Core::CoreErrorCode::Internal, message);
    }
    png_image_free(&image);
    png.resize(pngSize);
    std::ostringstream json;
    Core::JsonWriter writer(json);
    char fingerprintText[17]{};
    std::snprintf(fingerprintText, sizeof(fingerprintText), "%016llx", static_cast<unsigned long long>(fingerprint));
    writer.beginObject();
    writer.member("schemaVersion", UI::UIBakedFontSchemaVersion);
    writer.member("type", "msdf");
    writer.member("fontFingerprint", fingerprintText);
    writer.member("width", capacity.width);
    writer.member("height", capacity.height);
    writer.member("pixelsPerEm", UI::UITextMsdfPixelsPerEm);
    writer.member("distanceRange", UI::UITextMsdfDistanceRange);
    writer.member("glyphCount", entries.size());
    writer.member("missingGlyphCount", batch->missingGlyphCount);
    writer.member("origin", "top-left");
    writer.member("colorSpace", "linear-distance-data");
    writer.beginArrayMember("glyphs");
    for (const auto& entry : entries)
    {
        writer.beginObjectElement();
        writer.member("glyphIndex", entry.glyph.glyphIndex);
        writer.member("advance", entry.advance);
        writer.member("bearingXEm", entry.glyph.bearingXEm);
        writer.member("bearingYEm", entry.glyph.bearingYEm);
        writer.member("x", entry.placement.atlasX);
        writer.member("y", entry.placement.atlasY);
        writer.member("width", entry.glyph.width);
        writer.member("height", entry.glyph.height);
        writer.member("u0", static_cast<double>(entry.placement.atlasX) / capacity.width);
        writer.member("v0", static_cast<double>(entry.placement.atlasY) / capacity.height);
        writer.member("u1", static_cast<double>(entry.placement.atlasX + entry.glyph.width) / capacity.width);
        writer.member("v1", static_cast<double>(entry.placement.atlasY + entry.glyph.height) / capacity.height);
        writer.member("imageKind", static_cast<u32>(entry.glyph.imageKind));
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    if (writer.failed()) { return Core::failure(Core::CoreErrorCode::Internal, "MSDF metadata serialization failed"); }
    const std::string metadata = json.str();
    if (auto status = Core::writeFile(std::string(outputStem) + ".png", png); !status) { return status; }
    if (auto status = Core::writeFile(std::string(outputStem) + ".json", std::as_bytes(std::span(metadata))); !status) { return status; }
    if (auto status = Core::writeFile(std::string(outputStem) + ".tmsdf", cooked); !status) { return status; }
    std::cout << "msdfgen glyphs=" << entries.size() << " missing=" << batch->missingGlyphCount
              << " imageBytes=" << pixels.size() << '\n';
    return Core::success();
}

int run(int argc, char** argv)
{
    std::string font, strings, output;
    Core::ArgScanner arguments(argc, argv);
    while (arguments.next())
    {
        if (auto value = arguments.value("--font")) { font = *value; }
        else if (auto value = arguments.value("--strings")) { strings = *value; }
        else if (auto value = arguments.value("--output")) { output = *value; }
        else { std::cerr << "Unknown or incomplete font cook option\n"; return 2; }
    }
    if (font.empty() || strings.empty() || output.empty())
    {
        std::cerr << "Usage: tina_msdfgen --font FONT.ttf --strings UTF8.txt --output STEM\n";
        return 2;
    }
    if (auto status = bake(font, strings, output); !status)
    {
        std::cerr << status.error().message << '\n';
        return 1;
    }
    return 0;
}
}

int main(int argc, char** argv)
try
{
#if defined(_WIN32)
    // Windows argv is otherwise ACP-encoded even with /utf-8. Convert the
    // actual UTF-16 command line, then use Core UTF-8 paths and binary IO.
    int wideCount = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (wide == nullptr) { return 2; }
    struct ArgumentsOwner { wchar_t** value; ~ArgumentsOwner() { LocalFree(value); } } owner{wide};
    std::vector<std::string> strings;
    strings.reserve(wideCount);
    for (int index = 0; index < wideCount; ++index)
    {
        const std::u16string_view argument(reinterpret_cast<const char16_t*>(wide[index]));
        std::string utf8(argument.size() * 3U, '\0');
        auto bytes = Tina::Core::convertUtf16ToStrictUtf8(argument, std::span<char>(utf8));
        if (!bytes) { return 2; }
        utf8.resize(*bytes);
        strings.push_back(std::move(utf8));
    }
    std::vector<char*> utf8Arguments;
    for (auto& argument : strings) { utf8Arguments.push_back(argument.data()); }
    return run(wideCount, utf8Arguments.data());
#else
    return run(argc, argv);
#endif
}
catch (const std::exception& error)
{
    std::cerr << "Font preprocessing failed: " << error.what() << '\n';
    return 1;
}
