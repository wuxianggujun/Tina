#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIErrors.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace Tina::UI {
namespace {

[[nodiscard]] Core::Status invalidFont(const char* message)
{
    return Core::failure(UIErrorCode::InvalidFont, message);
}

[[nodiscard]] Core::Status invalidText(const char* message)
{
    return Core::failure(UIErrorCode::InvalidText, message);
}

class FreeTypeTextRasterizer final : public IUITextRasterizer {
  public:
    FreeTypeTextRasterizer(
        UITextRasterizerCapacity capacity,
        FT_Library library,
        std::pmr::memory_resource& resource)
        : m_capacity(capacity),
          m_library(library),
          m_faces(&resource),
          m_glyphs(&resource),
          m_coverage(&resource)
    {
        m_faces.resize(capacity.faceCapacity);
        m_glyphs.resize(capacity.maxGlyphsPerRaster);
        m_coverage.resize(capacity.coverageByteCapacity, 0);
    }

    ~FreeTypeTextRasterizer() override
    {
        for (FaceSlot& slot : m_faces) {
            if (slot.face != nullptr) {
                FT_Done_Face(slot.face);
                slot.face = nullptr;
            }
            slot.active = false;
        }
        if (m_library != nullptr) {
            FT_Done_FreeType(m_library);
            m_library = nullptr;
        }
    }

    [[nodiscard]] Core::Result<UIFontFaceId> openFace(
        std::span<const std::byte> fontBytes,
        i32 faceIndex) override
    {
        if (fontBytes.empty()) {
            return Core::failure(
                UIErrorCode::InvalidFont,
                "FreeType text rasterizer requires non-empty font bytes");
        }
        if (faceIndex < 0) {
            return Core::failure(
                UIErrorCode::InvalidFont,
                "FreeType face index must be non-negative");
        }

        for (u32 index = 0; index < static_cast<u32>(m_faces.size()); ++index) {
            FaceSlot& slot = m_faces[index];
            if (slot.active) {
                continue;
            }
            FT_Face face = nullptr;
            const FT_Error error = FT_New_Memory_Face(
                m_library,
                reinterpret_cast<const FT_Byte*>(fontBytes.data()),
                static_cast<FT_Long>(fontBytes.size()),
                faceIndex,
                &face);
            if (error != 0 || face == nullptr) {
                return Core::failure(
                    UIErrorCode::InvalidFont,
                    "FreeType failed to open the font face from memory");
            }
            if (slot.generation == (std::numeric_limits<u32>::max)()) {
                FT_Done_Face(face);
                return Core::failure(
                    UIErrorCode::CapacityExceeded,
                    "UI font face generation space is exhausted");
            }
            ++slot.generation;
            if (slot.generation == 0) {
                ++slot.generation;
            }
            slot.face = face;
            slot.active = true;
            return UIFontFaceId{.index = index, .generation = slot.generation};
        }
        return Core::failure(
            UIErrorCode::CapacityExceeded,
            "UI text rasterizer face capacity has been exhausted");
    }

    [[nodiscard]] Core::Status closeFace(UIFontFaceId face) noexcept override
    {
        FaceSlot* slot = resolveFace(face);
        if (slot == nullptr) {
            return invalidFont("UI font face is invalid or already closed");
        }
        if (slot->face != nullptr) {
            FT_Done_Face(slot->face);
            slot->face = nullptr;
        }
        slot->active = false;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITextMetrics> measure(
        UIFontFaceId faceId,
        std::string_view utf8,
        UITextStyle style) override
    {
        FaceSlot* slot = resolveFace(faceId);
        if (slot == nullptr || slot->face == nullptr) {
            return Core::failure(
                UIErrorCode::InvalidFont,
                "UI font face is invalid or closed");
        }
        if (Core::Status status = prepareFace(slot->face, style); !status) {
            return Core::failure(status.error());
        }

        if (!Core::isStrictUtf8WithoutNul(utf8)) {
            return Core::failure(
                UIErrorCode::InvalidText,
                "UI text must be strict UTF-8 without embedded NUL");
        }

        const float pixelSize = style.logicalSize;
        const float lineHeight = pixelSize * style.lineHeightScale;
        if (!(std::isfinite(pixelSize) && pixelSize > 0.0F && std::isfinite(lineHeight)
              && lineHeight > 0.0F)) {
            return Core::failure(
                UIErrorCode::InvalidText,
                "UI text style size and scales must be finite and positive");
        }

        float maxLineWidth = 0.0F;
        float currentLineWidth = 0.0F;
        u32 lineCount = utf8.empty() ? 0U : 1U;
        u32 codepointCount = 0;
        usize index = 0;
        while (index < utf8.size()) {
            char32_t codepoint = 0;
            usize unitLength = 0;
            if (Core::Status status =
                    decodeCodepoint(utf8, index, codepoint, unitLength);
                !status) {
                return Core::failure(status.error());
            }
            if (codepoint == '\n') {
                maxLineWidth = (std::max)(maxLineWidth, currentLineWidth);
                currentLineWidth = 0.0F;
                if (lineCount == (std::numeric_limits<u32>::max)()) {
                    return Core::failure(
                        UIErrorCode::InvalidText,
                        "UI text line count overflowed");
                }
                ++lineCount;
            } else {
                const float advance = glyphAdvance(slot->face, codepoint);
                if (!std::isfinite(advance)) {
                    return Core::failure(
                        UIErrorCode::InvalidText,
                        "UI text metrics overflowed finite float range");
                }
                currentLineWidth += advance;
                ++codepointCount;
            }
            index += unitLength;
        }
        maxLineWidth = (std::max)(maxLineWidth, currentLineWidth);

        if (utf8.empty()) {
            return UITextMetrics{};
        }
        return UITextMetrics{
            .measuredSize =
                UILogicalSize{
                    .width = maxLineWidth,
                    .height = lineHeight * static_cast<float>(lineCount),
                },
            .codepointCount = codepointCount,
            .lineCount = lineCount,
        };
    }

    [[nodiscard]] Core::Result<UITextRasterBatch> raster(
        UIFontFaceId faceId,
        std::string_view utf8,
        UITextStyle style) override
    {
        FaceSlot* slot = resolveFace(faceId);
        if (slot == nullptr || slot->face == nullptr) {
            return Core::failure(
                UIErrorCode::InvalidFont,
                "UI font face is invalid or closed");
        }
        if (Core::Status status = prepareFace(slot->face, style); !status) {
            return Core::failure(status.error());
        }

        auto metrics = measure(faceId, utf8, style);
        if (!metrics) {
            return Core::failure(metrics.error());
        }

        u32 glyphCount = 0;
        u32 coverageUsed = 0;
        usize index = 0;
        while (index < utf8.size()) {
            char32_t codepoint = 0;
            usize unitLength = 0;
            if (Core::Status status =
                    decodeCodepoint(utf8, index, codepoint, unitLength);
                !status) {
                return Core::failure(status.error());
            }
            if (codepoint == '\n') {
                index += unitLength;
                continue;
            }

            if (glyphCount >= m_capacity.maxGlyphsPerRaster) {
                return Core::failure(
                    UIErrorCode::CapacityExceeded,
                    "UI text raster glyph capacity has been exhausted");
            }

            const FT_UInt glyphIndex =
                FT_Get_Char_Index(slot->face, static_cast<FT_ULong>(codepoint));
            if (FT_Load_Glyph(slot->face, glyphIndex, FT_LOAD_DEFAULT) != 0) {
                return Core::failure(
                    UIErrorCode::InvalidFont,
                    "FreeType failed to load a glyph");
            }
            if (FT_Render_Glyph(slot->face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
                return Core::failure(
                    UIErrorCode::InvalidFont,
                    "FreeType failed to render a glyph");
            }

            const FT_GlyphSlot g = slot->face->glyph;
            const FT_Bitmap& bitmap = g->bitmap;
            const u32 width = bitmap.width;
            const u32 height = bitmap.rows;
            const u64 bytes = static_cast<u64>(width) * static_cast<u64>(height);
            if (coverageUsed > m_capacity.coverageByteCapacity
                || bytes > static_cast<u64>(m_capacity.coverageByteCapacity - coverageUsed)) {
                return Core::failure(
                    UIErrorCode::CapacityExceeded,
                    "UI text raster coverage capacity has been exhausted");
            }

            const u32 offset = coverageUsed;
            if (width > 0 && height > 0 && bitmap.buffer != nullptr) {
                for (u32 row = 0; row < height; ++row) {
                    const u8* src = bitmap.buffer + static_cast<usize>(row) * bitmap.pitch;
                    u8* dst = m_coverage.data() + offset + static_cast<usize>(row) * width;
                    if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
                        std::memcpy(dst, src, width);
                    } else if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                        for (u32 col = 0; col < width; ++col) {
                            const u8 bit = static_cast<u8>(
                                (src[col >> 3] >> (7 - (col & 7))) & 1U);
                            dst[col] = bit != 0 ? 255 : 0;
                        }
                    } else {
                        return Core::failure(
                            UIErrorCode::InvalidFont,
                            "Unsupported FreeType pixel mode");
                    }
                }
            }

            const float advance =
                static_cast<float>(g->advance.x) / 64.0F;
            m_glyphs[glyphCount] = UITextGlyphRaster{
                .codepoint = static_cast<u32>(codepoint),
                .advance = advance,
                .bearingX = static_cast<float>(g->bitmap_left),
                .bearingY = static_cast<float>(g->bitmap_top),
                .width = width,
                .height = height,
                .coverageOffset = offset,
                .coveragePitch = width,
            };
            ++glyphCount;
            coverageUsed += static_cast<u32>(bytes);
            index += unitLength;
        }

        return UITextRasterBatch{
            .metrics = *metrics,
            .glyphs = std::span<const UITextGlyphRaster>{m_glyphs.data(), glyphCount},
            .coverage = std::span<const u8>{m_coverage.data(), coverageUsed},
        };
    }

    [[nodiscard]] UITextRasterizerCapacity capacity() const noexcept override
    {
        return m_capacity;
    }

  private:
    struct FaceSlot final {
        FT_Face face = nullptr;
        u32 generation = 0;
        bool active = false;
    };

    [[nodiscard]] FaceSlot* resolveFace(UIFontFaceId face) noexcept
    {
        if (!face.hasValue() || face.index >= m_faces.size()) {
            return nullptr;
        }
        FaceSlot& slot = m_faces[face.index];
        if (!slot.active || slot.generation != face.generation) {
            return nullptr;
        }
        return &slot;
    }

    [[nodiscard]] const FaceSlot* resolveFace(UIFontFaceId face) const noexcept
    {
        return const_cast<FreeTypeTextRasterizer*>(this)->resolveFace(face);
    }

    [[nodiscard]] static Core::Status prepareFace(FT_Face face, UITextStyle style)
    {
        if (!(std::isfinite(style.logicalSize) && style.logicalSize > 0.0F
              && std::isfinite(style.lineHeightScale) && style.lineHeightScale > 0.0F
              && std::isfinite(style.advanceScale) && style.advanceScale > 0.0F)) {
            return invalidText(
                "UI text style size and scales must be finite and positive");
        }
        const FT_UInt pixelSize = static_cast<FT_UInt>(std::max(1.0F, style.logicalSize));
        if (FT_Set_Pixel_Sizes(face, 0, pixelSize) != 0) {
            return invalidFont("FreeType failed to set pixel sizes");
        }
        return Core::success();
    }

    [[nodiscard]] static float glyphAdvance(FT_Face face, char32_t codepoint)
    {
        const FT_UInt glyphIndex =
            FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint));
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) != 0) {
            return 0.0F;
        }
        return static_cast<float>(face->glyph->advance.x) / 64.0F;
    }

    [[nodiscard]] static Core::Status decodeCodepoint(
        std::string_view utf8,
        usize index,
        char32_t& codepoint,
        usize& unitLength)
    {
        if (index >= utf8.size()) {
            return invalidText("UI text must be strict UTF-8 without embedded NUL");
        }
        const auto first = static_cast<unsigned char>(utf8[index]);
        if (first <= 0x7FU) {
            if (first == 0U) {
                return invalidText("UI text must be strict UTF-8 without embedded NUL");
            }
            unitLength = 1;
            codepoint = first;
            return Core::success();
        }
        if ((first & 0xE0U) == 0xC0U) {
            unitLength = 2;
            codepoint = first & 0x1FU;
        } else if ((first & 0xF0U) == 0xE0U) {
            unitLength = 3;
            codepoint = first & 0x0FU;
        } else if ((first & 0xF8U) == 0xF0U) {
            unitLength = 4;
            codepoint = first & 0x07U;
        } else {
            return invalidText("UI text must be strict UTF-8 without embedded NUL");
        }
        if (unitLength > utf8.size() - index) {
            return invalidText("UI text must be strict UTF-8 without embedded NUL");
        }
        for (usize offset = 1; offset < unitLength; ++offset) {
            const auto next = static_cast<unsigned char>(utf8[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                return invalidText("UI text must be strict UTF-8 without embedded NUL");
            }
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }
        return Core::success();
    }

    UITextRasterizerCapacity m_capacity{};
    FT_Library m_library = nullptr;
    std::pmr::vector<FaceSlot> m_faces;
    std::pmr::vector<UITextGlyphRaster> m_glyphs;
    std::pmr::vector<u8> m_coverage;
};

} // namespace

Core::Result<std::unique_ptr<IUITextRasterizer>> createFreeTypeTextRasterizer(
    UITextRasterizerCapacity capacity,
    std::pmr::memory_resource& resource)
{
    if (Core::Status status = validateUITextRasterizerCapacity(capacity); !status) {
        return Core::failure(status.error());
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0 || library == nullptr) {
        return Core::failure(
            UIErrorCode::InvalidFont,
            "FreeType library initialization failed");
    }

    return std::unique_ptr<IUITextRasterizer>(
        new FreeTypeTextRasterizer(capacity, library, resource));
}

} // namespace Tina::UI
