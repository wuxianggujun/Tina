#pragma once

#include <tina/ui/text/UITextRasterizer.hpp>

namespace Tina::UI {

struct ShapedGlyph final {
    UIFontFaceId face{};
    u32 glyphIndex = 0;
    u32 clusterByteBegin = 0;
    u32 clusterByteEnd = 0;
    u32 line = 0;
    float x = 0.0F;
    float y = 0.0F;
    float advanceX = 0.0F;
    float advanceY = 0.0F;
    float offsetX = 0.0F;
    float offsetY = 0.0F;
};

struct GlyphRun final {
    UITextMetrics metrics{};
    float baselineFromLineTop = 0.0F;
    std::span<const ShapedGlyph> glyphs{};
    std::span<const UITextScalarMetrics> scalars{};
    u32 missingGlyphCount = 0;
};

namespace Detail { class FreeTypeTextRasterizer; }

// Owner-thread font and shaping owner. UTF-8 -> BiDi/script/fallback runs ->
// OpenType shaping -> visual glyphs + logical caret map. Library types remain
// private. Font bytes are COPIED; callers may release their input after openFace.
class TextShaper final {
  public:
    [[nodiscard]] static Core::Result<std::unique_ptr<TextShaper>> Create(
        UITextRasterizerCapacity capacity = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
    ~TextShaper();
    TextShaper(const TextShaper&) = delete;
    TextShaper& operator=(const TextShaper&) = delete;
    TextShaper(TextShaper&&) = delete;
    TextShaper& operator=(TextShaper&&) = delete;

    [[nodiscard]] Core::Result<UIFontFaceId> openFace(
        std::span<const std::byte> bytes, i32 faceIndex = 0);
    [[nodiscard]] Core::Status closeFace(UIFontFaceId face) noexcept;
    [[nodiscard]] Core::Status setFallbackChain(std::span<const UIFontFaceId> faces);
    // Result borrows owner storage until the next shape/font mutation. The
    // bounded string-intern cache compares full UTF-8 bytes, not just hashes.
    [[nodiscard]] Core::Result<GlyphRun> shape(
        UIFontFaceId primary, std::string_view utf8, UITextStyle style);

  private:
    friend class Detail::FreeTypeTextRasterizer;
    struct Impl;
    explicit TextShaper(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Tina::UI
