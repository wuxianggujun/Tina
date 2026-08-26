#pragma once

// CPU-side glyph atlas for UI text. Stores R8 coverage only; GPU texture upload
// and Glyph DisplayList commands remain a later slice. FreeType types never
// appear in this header.

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <compare>
#include <memory>
#include <memory_resource>
#include <span>

namespace Tina::UI {

// Strong id for one atlas glyph slot. hasValue() only means bits are non-zero;
// the owning atlas must re-resolve generation.
struct UIGlyphId final {
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return generation != 0;
    }

    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    auto operator<=>(const UIGlyphId&) const = default;
};

// Cache key for a rasterized glyph. pixelSize is quantized logical size used at
// raster time (usually UITextStyle.logicalSize).
struct UIGlyphKey final {
    UIFontFaceId face{};
    u32 codepoint = 0;
    u32 pixelSize = 0;

    auto operator<=>(const UIGlyphKey&) const = default;
};

struct UIGlyphAtlasCapacity final {
    static constexpr u32 DefaultWidth = 512;
    static constexpr u32 DefaultHeight = 512;
    static constexpr u32 DefaultMaxGlyphs = 1024;
    static constexpr u32 MaxWidth = 4096;
    static constexpr u32 MaxHeight = 4096;
    static constexpr u32 MaxGlyphs = 1'048'576;

    u32 width = DefaultWidth;
    u32 height = DefaultHeight;
    u32 maxGlyphs = DefaultMaxGlyphs;
};

// Placement of one glyph inside the atlas page. UV is in texel space (not
// normalized); UV origin is top-left of the glyph rect inside the page.
struct UIGlyphPlacement final {
    UIGlyphId id{};
    UIGlyphKey key{};
    u32 atlasX = 0;
    u32 atlasY = 0;
    u32 width = 0;
    u32 height = 0;
    float advance = 0.0F;
    float bearingX = 0.0F;
    float bearingY = 0.0F;
};

struct UIGlyphAtlasStatistics final {
    u32 width = 0;
    u32 height = 0;
    u32 maxGlyphs = 0;
    u32 glyphCount = 0;
    u32 glyphHighWater = 0;
    u32 usedPixels = 0;
    u32 usedPixelHighWater = 0;
    u32 shelfCount = 0;
};

// Fixed-capacity shelf-packed R8 atlas. Create pre-reserves the full page and
// glyph table; insert never heap-grows. Packing failure returns CapacityExceeded
// without mutating published slots.
class UIGlyphAtlas final {
  public:
    [[nodiscard]] static Core::Result<std::unique_ptr<UIGlyphAtlas>> Create(
        UIGlyphAtlasCapacity capacity = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~UIGlyphAtlas() = default;

    UIGlyphAtlas(const UIGlyphAtlas&) = delete;
    UIGlyphAtlas& operator=(const UIGlyphAtlas&) = delete;
    UIGlyphAtlas(UIGlyphAtlas&&) = delete;
    UIGlyphAtlas& operator=(UIGlyphAtlas&&) = delete;

    // Insert or return the existing placement for key. Coverage must be
    // row-major R8 with pitch == glyph.width and size >= width*height.
    // Zero-sized glyphs (space advance only) are allowed and store no pixels.
    [[nodiscard]] Core::Result<UIGlyphPlacement> insert(
        const UIGlyphKey& key,
        const UITextGlyphRaster& glyph,
        std::span<const u8> coverage);

    [[nodiscard]] Core::Result<UIGlyphPlacement> find(const UIGlyphKey& key) const noexcept;

    [[nodiscard]] bool contains(UIGlyphId id) const noexcept;

    // Invalidates every placement (generation bump). Page pixels are zeroed.
    // Does not free the fixed Create-time capacity.
    void clear() noexcept;

    [[nodiscard]] UIGlyphAtlasCapacity capacity() const noexcept;
    [[nodiscard]] UIGlyphAtlasStatistics statistics() const noexcept;

    // Borrowed R8 page. Valid until the next clear() or atlas destruction.
    // Size is always width*height.
    [[nodiscard]] std::span<const u8> pagePixels() const noexcept;

    // Monotonic counter bumped whenever page pixels change. The page is
    // allocated at full size up front, so emptiness cannot signal "nothing to
    // upload"; consumers compare this instead of re-uploading every frame.
    [[nodiscard]] u64 pageRevision() const noexcept;

  private:
    UIGlyphAtlas(
        UIGlyphAtlasCapacity capacity,
        std::pmr::memory_resource& resource);

    struct GlyphSlot final {
        UIGlyphKey key{};
        u32 generation = 0;
        u32 atlasX = 0;
        u32 atlasY = 0;
        u32 width = 0;
        u32 height = 0;
        float advance = 0.0F;
        float bearingX = 0.0F;
        float bearingY = 0.0F;
        bool active = false;
    };

    struct Shelf final {
        u32 y = 0;
        u32 height = 0;
        u32 x = 0;
    };

    [[nodiscard]] Core::Result<UIGlyphPlacement> placeNew(
        const UIGlyphKey& key,
        const UITextGlyphRaster& glyph,
        std::span<const u8> coverage);

    [[nodiscard]] const GlyphSlot* resolve(UIGlyphId id) const noexcept;
    [[nodiscard]] GlyphSlot* resolve(UIGlyphId id) noexcept;

    UIGlyphAtlasCapacity m_capacity{};
    std::pmr::vector<GlyphSlot> m_slots;
    std::pmr::vector<u32> m_freeSlots;
    std::pmr::vector<Shelf> m_shelves;
    std::pmr::vector<u8> m_page;
    u32 m_glyphCount = 0;
    u32 m_glyphHighWater = 0;
    u32 m_usedPixels = 0;
    u32 m_usedPixelHighWater = 0;
    u32 m_nextShelfY = 0;
    // Starts at 1 so a consumer's zero-initialized "last uploaded" value always
    // differs from the first published revision.
    u64 m_pageRevision = 1;
};

[[nodiscard]] Core::Status validateUIGlyphAtlasCapacity(
    const UIGlyphAtlasCapacity& capacity);

} // namespace Tina::UI
