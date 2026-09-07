#pragma once

#include <tina/ui/text/TextShaper.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <fribidi.h>

#include <array>
#include <vector>

namespace Tina::UI {

struct TextShaper::Impl final {
    struct Face final {
        explicit Face(std::pmr::memory_resource& resource) : bytes(&resource) {}
        ~Face();
        std::pmr::vector<std::byte> bytes;
        FT_Face ft = nullptr;
        hb_font_t* hb = nullptr;
        u32 unitsPerEm = 0;
        i32 faceIndex = 0;
    };
    struct Segment final {
        u32 begin = 0;
        u32 end = 0;
        UIFontFaceId face{};
        hb_script_t script = HB_SCRIPT_UNKNOWN;
        FriBidiLevel level = 0;
    };
    struct InternedRun final {
        UIFontFaceId primary{};
        UITextStyle style{};
        u32 textBegin = 0;
        u32 textLength = 0;
        u32 glyphBegin = 0;
        u32 glyphCount = 0;
        u32 scalarBegin = 0;
        u32 scalarCount = 0;
        UITextMetrics metrics{};
        float baseline = 0.0F;
        u32 missing = 0;
    };

    Impl(UITextRasterizerCapacity capacity, std::pmr::memory_resource& resource);
    ~Impl();
    [[nodiscard]] Face* face(UIFontFaceId id) noexcept;
    [[nodiscard]] Core::Result<GlyphRun> shape(
        UIFontFaceId primary, std::string_view utf8, UITextStyle style);
    [[nodiscard]] Core::Status shapeLine(
        UIFontFaceId primary, std::string_view utf8, u32 beginByte,
        u32 endByte, u32 line, UITextStyle style, float& width, u32& missing);
    [[nodiscard]] UIFontFaceId chooseFace(
        UIFontFaceId primary, u32 begin, u32 end) noexcept;
    void invalidateCache() noexcept;

    UITextRasterizerCapacity capacity;
    std::pmr::memory_resource& resource;
    FT_Library library = nullptr;
    hb_buffer_t* buffer = nullptr;
    std::pmr::vector<std::unique_ptr<Face>> faces;
    std::pmr::vector<u32> generations;
    std::array<UIFontFaceId, UITextRasterizerCapacity::MaxFaceCapacity> fallback{};
    usize fallbackCount = 0;
    std::pmr::vector<FriBidiChar> characters;
    std::pmr::vector<u32> byteOffsets;
    std::pmr::vector<FriBidiCharType> bidiTypes;
    std::pmr::vector<FriBidiBracketType> brackets;
    std::pmr::vector<FriBidiLevel> levels;
    std::pmr::vector<hb_script_t> scripts;
    std::pmr::vector<UIFontFaceId> selectedFaces;
    std::pmr::vector<Segment> segments;
    std::pmr::vector<u32> clusterBoundaries;
    std::pmr::vector<ShapedGlyph> glyphs;
    std::pmr::vector<UITextScalarMetrics> scalars;
    std::pmr::vector<InternedRun> interned;
    std::pmr::vector<char> internedText;
    std::pmr::vector<ShapedGlyph> internedGlyphs;
    std::pmr::vector<UITextScalarMetrics> internedScalars;
};

} // namespace Tina::UI
