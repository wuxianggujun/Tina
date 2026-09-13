#include <tina/ui/text/UITextRasterizer.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace Tina::UI {
namespace {

[[nodiscard]] Core::Status invalidConfig(const char* message)
{
    return Core::failure(UIErrorCode::InvalidContextConfig, message);
}

[[nodiscard]] Core::Status invalidFont(const char* message)
{
    return Core::failure(UIErrorCode::InvalidFont, message);
}

// Placeholder cells are whole device pixels. Clamp through double so an absurd
// style*scale product cannot make the u32 conversion undefined; the coverage
// capacity check in raster() rejects it as CapacityExceeded afterwards.
[[nodiscard]] u32 deviceCellExtent(float logical, float scale) noexcept
{
    const double device = std::floor(static_cast<double>(logical) * static_cast<double>(scale));
    if (!std::isfinite(device) || device < 1.0) {
        return 1U;
    }
    return static_cast<u32>(
        (std::min)(device, static_cast<double>((std::numeric_limits<u32>::max)())));
}

class PlaceholderTextRasterizer final : public IUITextRasterizer {
  public:
    PlaceholderTextRasterizer(
        UITextRasterizerCapacity capacity,
        std::pmr::memory_resource& resource)
        : m_capacity(capacity),
          m_faces(&resource),
          m_glyphs(&resource),
          m_scalars(&resource),
          m_coverage(&resource)
    {
        m_faces.reserve(capacity.initialFaceCapacity);
        m_glyphs.reserve(capacity.maxGlyphsPerRaster);
        m_scalars.reserve(capacity.maxGlyphsPerRaster);
        m_coverage.resize(capacity.coverageByteCapacity, 0);
    }

    [[nodiscard]] Core::Result<UIFontFaceId> openFace(
        std::span<const std::byte> fontBytes,
        i32 faceIndex) override
    {
        if (!fontBytes.empty()) {
            return Core::failure(
                UIErrorCode::InvalidFont,
                "Placeholder text rasterizer only accepts empty font bytes for the built-in face");
        }
        if (faceIndex != 0) {
            return Core::failure(
                UIErrorCode::InvalidFont,
                "Placeholder text rasterizer only supports face index 0");
        }

        for (u32 index = 0; index < static_cast<u32>(m_faces.size()); ++index) {
            FaceSlot& slot = m_faces[index];
            if (slot.active) {
                continue;
            }
            if (slot.generation == (std::numeric_limits<u32>::max)()) { continue; }
            ++slot.generation;
            if (slot.generation == 0) {
                ++slot.generation;
            }
            slot.active = true;
            return UIFontFaceId{.index = index, .generation = slot.generation};
        }
        if (m_faces.size() == (std::numeric_limits<u32>::max)())
        { return Core::failure(UIErrorCode::CapacityExceeded, "Font face identity space exhausted"); }
        try
        {
            m_faces.push_back(FaceSlot{.generation = 1, .active = true});
            return UIFontFaceId{static_cast<u32>(m_faces.size() - 1U), 1};
        }
        catch (const std::bad_alloc&)
        { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Placeholder font face allocation failed"); }
        catch (const std::length_error&)
        { return Core::failure(UIErrorCode::CapacityExceeded, "Font face storage exceeds addressable size"); }
    }

    [[nodiscard]] Core::Status closeFace(UIFontFaceId face) noexcept override
    {
        FaceSlot* slot = resolveFace(face);
        if (slot == nullptr) {
            return invalidFont("UI font face is invalid or already closed");
        }
        slot->active = false;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITextMetrics> measure(
        UIFontFaceId face,
        std::string_view utf8,
        UITextStyle style,
        UITextRasterScale scale) override
    {
        if (resolveFace(face) == nullptr) {
            return Core::failure(
                UIErrorCode::InvalidFont,
                "UI font face is invalid or closed");
        }
        if (Core::Status status = validateUITextRasterScale(scale); !status) {
            return Core::failure(status.error());
        }
        if (utf8.size() > m_capacity.maxTextBytes) {
            return Core::failure(UIErrorCode::CapacityExceeded, "Placeholder text exceeds its byte budget");
        }
        // Placeholder metrics are pure logical arithmetic; the scale only
        // changes how many device pixels a cell occupies in raster().
        return measurePlaceholderText(utf8, style);
    }

    Core::Status setFallbackChain(std::span<const UIFontFaceId> faces) override
    {
        for (usize index = 0; index < faces.size(); ++index)
        {
            if (resolveFace(faces[index]) == nullptr ||
                std::find(faces.begin(), faces.begin() + index, faces[index]) != faces.begin() + index)
            { return invalidFont("Placeholder fallback chain contains a stale or duplicate face"); }
        }
        return Core::success();
    }

    Core::Status primeGlyphCache(UIFontFaceId, std::span<const std::byte>) override
    {
        return invalidFont("The explicit placeholder does not accept cooked font glyphs");
    }

    [[nodiscard]] Core::Result<UITextShapeView> shape(
        UIFontFaceId face, std::string_view utf8, UITextStyle style) override
    try
    {
        if (resolveFace(face) == nullptr) {
            return Core::failure(
                UIErrorCode::InvalidFont,
                "UI font face is invalid or closed");
        }
        if (utf8.size() > m_capacity.maxTextBytes) {
            return Core::failure(UIErrorCode::CapacityExceeded, "Placeholder text exceeds its byte budget");
        }
        auto metrics = measurePlaceholderText(utf8, style);
        if (!metrics) {
            return Core::failure(metrics.error());
        }
        const usize scalarCount = metrics->codepointCount - (metrics->lineCount == 0 ? 0U : metrics->lineCount - 1U);
        m_glyphs.resize(scalarCount);
        m_scalars.resize(scalarCount);

        const float advance = style.logicalSize * style.advanceScale;
        const float lineHeight = style.logicalSize * style.lineHeightScale;
        u32 glyphCount = 0;
        u32 line = 0;
        float penX = 0.0F;
        usize index = 0;
        while (index < utf8.size()) {
            const auto first = static_cast<unsigned char>(utf8[index]);
            usize unitLength = 1;
            char32_t codepoint = first;
            if (first <= 0x7FU) {
                unitLength = 1;
            } else if ((first & 0xE0U) == 0xC0U) {
                unitLength = 2;
                codepoint = first & 0x1FU;
            } else if ((first & 0xF0U) == 0xE0U) {
                unitLength = 3;
                codepoint = first & 0x0FU;
            } else if ((first & 0xF8U) == 0xF0U) {
                unitLength = 4;
                codepoint = first & 0x07U;
            } else {
                return Core::failure(
                    UIErrorCode::InvalidText,
                    "UI text must be strict UTF-8 without embedded NUL");
            }
            if (unitLength > utf8.size() - index) {
                return Core::failure(
                    UIErrorCode::InvalidText,
                    "UI text must be strict UTF-8 without embedded NUL");
            }
            for (usize offset = 1; offset < unitLength; ++offset) {
                const auto next = static_cast<unsigned char>(utf8[index + offset]);
                if ((next & 0xC0U) != 0x80U) {
                    return Core::failure(
                        UIErrorCode::InvalidText,
                        "UI text must be strict UTF-8 without embedded NUL");
                }
                codepoint = (codepoint << 6U) | (next & 0x3FU);
            }

            if (!(unitLength == 1 && first == '\n')) {
                m_glyphs[glyphCount] = UITextGlyphRaster{
                    .face = face,
                    .glyphIndex = static_cast<u32>(codepoint),
                    .clusterByteBegin = static_cast<u32>(index),
                    .clusterByteEnd = static_cast<u32>(index + unitLength),
                    .line = line,
                    .originX = penX,
                    .originY = static_cast<float>(line) * lineHeight,
                    .advance = advance,
                    .bearingX = 0.0F,
                    .bearingY = lineHeight,
                };
                m_scalars[glyphCount] = UITextScalarMetrics{
                    .advance = advance,
                    .visualStartX = penX,
                    .visualEndX = penX + advance,
                    .clusterByteBegin = static_cast<u32>(index),
                    .clusterByteEnd = static_cast<u32>(index + unitLength),
                    .line = line,
                    .hasVisualPosition = true,
                };
                penX += advance;
                ++glyphCount;
            } else {
                ++line;
                penX = 0.0F;
            }
            index += unitLength;
        }

        return UITextShapeView{
            .metrics = *metrics,
            .baselineFromLineTop = lineHeight,
            .scalars = std::span<const UITextScalarMetrics>{m_scalars.data(), glyphCount},
        };
    }
    catch (const std::bad_alloc&)
    { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Placeholder shape allocation failed"); }
    catch (const std::length_error&)
    { return Core::failure(UIErrorCode::CapacityExceeded, "Placeholder shape exceeds addressable storage"); }

    [[nodiscard]] Core::Result<UITextRasterBatch> raster(
        UIFontFaceId face, std::string_view utf8, UITextStyle style,
        UITextRasterScale scale) override
    {
        if (auto status = validateUITextRasterScale(scale); !status) { return Core::failure(status.error()); }
        auto shaped = shape(face, utf8, style);
        if (!shaped) { return Core::failure(shaped.error()); }
        if (shaped->scalars.size() > m_capacity.maxGlyphsPerRaster)
        { return Core::failure(UIErrorCode::CapacityExceeded, "Raster glyph budget exhausted"); }
        const u32 cellWidth = deviceCellExtent(style.logicalSize * style.advanceScale, scale.x);
        const u32 cellHeight = deviceCellExtent(style.logicalSize * style.lineHeightScale, scale.y);
        if (cellWidth > m_capacity.coverageByteCapacity / 4U ||
            cellHeight > m_capacity.coverageByteCapacity / 4U)
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "Placeholder device extent exceeds its byte budget");
        }
        const u64 cellBytes = static_cast<u64>(cellWidth) * cellHeight * 4U;
        if (!shaped->scalars.empty() && cellBytes > m_capacity.coverageByteCapacity / shaped->scalars.size())
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "UI text raster coverage capacity has been exhausted");
        }
        const u32 coverageUsed = static_cast<u32>(cellBytes * shaped->scalars.size());
        std::memset(m_coverage.data(), 255, coverageUsed);
        for (usize index = 0; index < shaped->scalars.size(); ++index)
        {
            auto& glyph = m_glyphs[index];
            glyph.width = cellWidth;
            glyph.height = cellHeight;
            glyph.coverageOffset = static_cast<u32>(cellBytes * index);
            glyph.coveragePitch = cellWidth * 4U;
            glyph.logicalWidth = static_cast<float>(cellWidth) / scale.x;
            glyph.logicalHeight = static_cast<float>(cellHeight) / scale.y;
            glyph.rasterSize = {cellWidth, cellHeight};
        }
        return UITextRasterBatch{
            .metrics = shaped->metrics,
            .baselineFromLineTop = shaped->baselineFromLineTop,
            .glyphs = std::span<const UITextGlyphRaster>{m_glyphs.data(), shaped->scalars.size()},
            .scalars = shaped->scalars,
            .coverage = std::span<const u8>{m_coverage.data(), coverageUsed},
        };
    }

    [[nodiscard]] UITextRasterizerCapacity capacity() const noexcept override
    {
        return m_capacity;
    }

  private:
    struct FaceSlot final {
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
        return const_cast<PlaceholderTextRasterizer*>(this)->resolveFace(face);
    }

    UITextRasterizerCapacity m_capacity{};
    std::pmr::vector<FaceSlot> m_faces;
    std::pmr::vector<UITextGlyphRaster> m_glyphs;
    std::pmr::vector<UITextScalarMetrics> m_scalars;
    std::pmr::vector<u8> m_coverage;
};

} // namespace

Core::Status validateUITextRasterizerCapacity(const UITextRasterizerCapacity& capacity)
{
    if (capacity.maxGlyphsPerRaster == 0
        || capacity.coverageByteCapacity == 0 || capacity.glyphImageCapacity == 0 ||
        capacity.maxTextBytes == 0 || capacity.maxFontBytes == 0) {
        return invalidConfig("UI text rasterizer capacities must be greater than zero");
    }
    if (capacity.maxGlyphsPerRaster > UITextRasterizerCapacity::MaxGlyphsPerRaster
        || capacity.coverageByteCapacity > UITextRasterizerCapacity::MaxCoverageByteCapacity ||
        capacity.glyphImageCapacity > UITextRasterizerCapacity::MaxGlyphsPerRaster ||
        capacity.maxTextBytes > 4U * UITextRasterizerCapacity::MaxGlyphsPerRaster ||
        capacity.maxFontBytes > 256U * 1024U * 1024U) {
        return invalidConfig("UI text rasterizer capacity exceeds the configured maximum");
    }
    return Core::success();
}

Core::Status validateUITextRasterScale(UITextRasterScale scale)
{
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || scale.x <= 0.0F || scale.y <= 0.0F)
    {
        return Core::failure(UIErrorCode::InvalidText, "Text raster scale must be finite and positive");
    }
    return Core::success();
}

UIGlyphDevicePixelSize resolveGlyphDevicePixelSize(const UITextStyle& style, UITextRasterScale scale) noexcept
{
    const auto extent = [](double value) noexcept -> u32 {
        if (!std::isfinite(value)) { return (std::numeric_limits<u32>::max)(); }
        return static_cast<u32>(std::clamp(std::round(value), 1.0,
                                         static_cast<double>((std::numeric_limits<u32>::max)())));
    };
    return {extent(static_cast<double>(style.logicalSize) * scale.x),
            extent(static_cast<double>(style.logicalSize) * scale.y)};
}

Core::Result<std::unique_ptr<IUITextRasterizer>> createPlaceholderTextRasterizer(
    UITextRasterizerCapacity capacity,
    std::pmr::memory_resource& resource)
{
    if (Core::Status status = validateUITextRasterizerCapacity(capacity); !status) {
        return Core::failure(status.error());
    }
    try {
        auto* raw = new (std::nothrow) PlaceholderTextRasterizer(capacity, resource);
        if (raw == nullptr) {
            return Core::failure(
                Core::CoreErrorCode::OutOfMemory,
                "UI placeholder text rasterizer allocation failed");
        }
        return std::unique_ptr<IUITextRasterizer>(raw);
    } catch (const std::bad_alloc&) {
        return Core::failure(
            Core::CoreErrorCode::OutOfMemory,
            "UI placeholder text rasterizer allocation failed");
    } catch (const std::exception&) {
        return Core::failure(
            Core::CoreErrorCode::Internal,
            "UI placeholder text rasterizer construction failed");
    } catch (...) {
        return Core::failure(
            Core::CoreErrorCode::Internal,
            "UI placeholder text rasterizer construction failed");
    }
}

} // namespace Tina::UI
