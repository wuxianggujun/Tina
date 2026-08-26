#include <tina/ui/text/UIGlyphAtlas.hpp>

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace Tina::UI {
namespace {

[[nodiscard]] Core::Status invalidConfig(const char* message)
{
    return Core::failure(UIErrorCode::InvalidContextConfig, message);
}

[[nodiscard]] bool keysEqual(const UIGlyphKey& left, const UIGlyphKey& right) noexcept
{
    return left.face == right.face
        && left.codepoint == right.codepoint
        && left.pixelSize == right.pixelSize;
}

} // namespace

Core::Status validateUIGlyphAtlasCapacity(const UIGlyphAtlasCapacity& capacity)
{
    if (capacity.width == 0 || capacity.height == 0 || capacity.maxGlyphs == 0) {
        return invalidConfig("UI glyph atlas dimensions and max glyphs must be greater than zero");
    }
    if (capacity.width > UIGlyphAtlasCapacity::MaxWidth
        || capacity.height > UIGlyphAtlasCapacity::MaxHeight
        || capacity.maxGlyphs > UIGlyphAtlasCapacity::MaxGlyphs) {
        return invalidConfig("UI glyph atlas capacity exceeds the configured maximum");
    }
    const u64 pageBytes =
        static_cast<u64>(capacity.width) * static_cast<u64>(capacity.height);
    if (pageBytes > static_cast<u64>((std::numeric_limits<u32>::max)())) {
        return invalidConfig("UI glyph atlas page byte count overflows u32");
    }
    return Core::success();
}

UIGlyphAtlas::UIGlyphAtlas(
    UIGlyphAtlasCapacity capacity,
    std::pmr::memory_resource& resource)
    : m_capacity(capacity),
      m_slots(&resource),
      m_freeSlots(&resource),
      m_shelves(&resource),
      m_page(&resource)
{
    m_slots.resize(capacity.maxGlyphs);
    m_freeSlots.reserve(capacity.maxGlyphs);
    for (u32 index = capacity.maxGlyphs; index > 0; --index) {
        m_freeSlots.push_back(index - 1);
    }
    m_shelves.reserve(capacity.height);
    m_page.resize(
        static_cast<usize>(capacity.width) * static_cast<usize>(capacity.height),
        0);
}

Core::Result<std::unique_ptr<UIGlyphAtlas>> UIGlyphAtlas::Create(
    UIGlyphAtlasCapacity capacity,
    std::pmr::memory_resource& resource)
{
    if (Core::Status status = validateUIGlyphAtlasCapacity(capacity); !status) {
        return Core::failure(status.error());
    }
    try {
        return std::unique_ptr<UIGlyphAtlas>(new UIGlyphAtlas(capacity, resource));
    } catch (const std::bad_alloc&) {
        return Core::failure(
            Core::CoreErrorCode::OutOfMemory,
            "UI glyph atlas allocation failed");
    }
}

Core::Result<UIGlyphPlacement> UIGlyphAtlas::insert(
    const UIGlyphKey& key,
    const UITextGlyphRaster& glyph,
    std::span<const u8> coverage)
{
    if (!key.face.hasValue()) {
        return Core::failure(UIErrorCode::InvalidFont, "UI glyph key requires a live font face");
    }
    if (key.pixelSize == 0) {
        return Core::failure(UIErrorCode::InvalidText, "UI glyph key pixel size must be positive");
    }
    if (glyph.width > m_capacity.width || glyph.height > m_capacity.height) {
        return Core::failure(
            UIErrorCode::CapacityExceeded,
            "UI glyph is larger than the atlas page");
    }
    const u64 required =
        static_cast<u64>(glyph.width) * static_cast<u64>(glyph.height);
    if (glyph.width > 0 && glyph.height > 0) {
        if (glyph.coveragePitch != glyph.width) {
            return Core::failure(
                UIErrorCode::InvalidText,
                "UI glyph coverage pitch must equal glyph width for atlas insert");
        }
        if (coverage.size() < required) {
            return Core::failure(
                UIErrorCode::InvalidText,
                "UI glyph coverage buffer is shorter than width*height");
        }
    }

    if (auto existing = find(key); existing) {
        return *existing;
    }
    return placeNew(key, glyph, coverage);
}

Core::Result<UIGlyphPlacement> UIGlyphAtlas::placeNew(
    const UIGlyphKey& key,
    const UITextGlyphRaster& glyph,
    std::span<const u8> coverage)
{
    if (m_freeSlots.empty()) {
        return Core::failure(
            UIErrorCode::CapacityExceeded,
            "UI glyph atlas glyph capacity has been exhausted");
    }

    u32 atlasX = 0;
    u32 atlasY = 0;
    if (glyph.width > 0 && glyph.height > 0) {
        bool placed = false;
        for (Shelf& shelf : m_shelves) {
            if (glyph.height > shelf.height) {
                continue;
            }
            if (shelf.x > m_capacity.width
                || glyph.width > m_capacity.width - shelf.x) {
                continue;
            }
            atlasX = shelf.x;
            atlasY = shelf.y;
            shelf.x += glyph.width;
            placed = true;
            break;
        }
        if (!placed) {
            if (m_nextShelfY > m_capacity.height
                || glyph.height > m_capacity.height - m_nextShelfY) {
                return Core::failure(
                    UIErrorCode::CapacityExceeded,
                    "UI glyph atlas page packing has been exhausted");
            }
            if (glyph.width > m_capacity.width) {
                return Core::failure(
                    UIErrorCode::CapacityExceeded,
                    "UI glyph is wider than the atlas page");
            }
            Shelf shelf{
                .y = m_nextShelfY,
                .height = glyph.height,
                .x = glyph.width,
            };
            atlasX = 0;
            atlasY = m_nextShelfY;
            m_nextShelfY += glyph.height;
            m_shelves.push_back(shelf);
        }

        for (u32 row = 0; row < glyph.height; ++row) {
            const u8* src =
                coverage.data() + static_cast<usize>(row) * glyph.coveragePitch;
            u8* dst = m_page.data()
                + static_cast<usize>(atlasY + row) * m_capacity.width
                + atlasX;
            std::memcpy(dst, src, glyph.width);
        }
        ++m_pageRevision;
    }

    const u32 slotIndex = m_freeSlots.back();
    m_freeSlots.pop_back();
    GlyphSlot& slot = m_slots[slotIndex];
    if (slot.generation == (std::numeric_limits<u32>::max)()) {
        m_freeSlots.push_back(slotIndex);
        return Core::failure(
            UIErrorCode::CapacityExceeded,
            "UI glyph generation space is exhausted");
    }
    ++slot.generation;
    if (slot.generation == 0) {
        ++slot.generation;
    }
    slot.key = key;
    slot.atlasX = atlasX;
    slot.atlasY = atlasY;
    slot.width = glyph.width;
    slot.height = glyph.height;
    slot.advance = glyph.advance;
    slot.bearingX = glyph.bearingX;
    slot.bearingY = glyph.bearingY;
    slot.active = true;

    ++m_glyphCount;
    m_glyphHighWater = (std::max)(m_glyphHighWater, m_glyphCount);
    const u32 pixels = glyph.width * glyph.height;
    m_usedPixels += pixels;
    m_usedPixelHighWater = (std::max)(m_usedPixelHighWater, m_usedPixels);

    return UIGlyphPlacement{
        .id = UIGlyphId{.index = slotIndex, .generation = slot.generation},
        .key = key,
        .atlasX = atlasX,
        .atlasY = atlasY,
        .width = glyph.width,
        .height = glyph.height,
        .advance = glyph.advance,
        .bearingX = glyph.bearingX,
        .bearingY = glyph.bearingY,
    };
}

Core::Result<UIGlyphPlacement> UIGlyphAtlas::find(const UIGlyphKey& key) const noexcept
{
    for (u32 index = 0; index < static_cast<u32>(m_slots.size()); ++index) {
        const GlyphSlot& slot = m_slots[index];
        if (!slot.active || !keysEqual(slot.key, key)) {
            continue;
        }
        return UIGlyphPlacement{
            .id = UIGlyphId{.index = index, .generation = slot.generation},
            .key = slot.key,
            .atlasX = slot.atlasX,
            .atlasY = slot.atlasY,
            .width = slot.width,
            .height = slot.height,
            .advance = slot.advance,
            .bearingX = slot.bearingX,
            .bearingY = slot.bearingY,
        };
    }
    return Core::failure(UIErrorCode::InvalidNode, "UI glyph is not present in the atlas");
}

bool UIGlyphAtlas::contains(UIGlyphId id) const noexcept
{
    return resolve(id) != nullptr;
}

void UIGlyphAtlas::clear() noexcept
{
    for (GlyphSlot& slot : m_slots) {
        slot.active = false;
        // Keep generation so reused slots never collide with stale ids.
    }
    m_freeSlots.clear();
    for (u32 index = static_cast<u32>(m_slots.size()); index > 0; --index) {
        m_freeSlots.push_back(index - 1);
    }
    m_shelves.clear();
    m_nextShelfY = 0;
    m_glyphCount = 0;
    m_usedPixels = 0;
    std::memset(m_page.data(), 0, m_page.size());
    ++m_pageRevision;
}

UIGlyphAtlasCapacity UIGlyphAtlas::capacity() const noexcept
{
    return m_capacity;
}

UIGlyphAtlasStatistics UIGlyphAtlas::statistics() const noexcept
{
    return UIGlyphAtlasStatistics{
        .width = m_capacity.width,
        .height = m_capacity.height,
        .maxGlyphs = m_capacity.maxGlyphs,
        .glyphCount = m_glyphCount,
        .glyphHighWater = m_glyphHighWater,
        .usedPixels = m_usedPixels,
        .usedPixelHighWater = m_usedPixelHighWater,
        .shelfCount = static_cast<u32>(m_shelves.size()),
    };
}

std::span<const u8> UIGlyphAtlas::pagePixels() const noexcept
{
    return std::span<const u8>(m_page.data(), m_page.size());
}

u64 UIGlyphAtlas::pageRevision() const noexcept
{
    return m_pageRevision;
}

const UIGlyphAtlas::GlyphSlot* UIGlyphAtlas::resolve(UIGlyphId id) const noexcept
{
    if (!id.hasValue() || id.index >= m_slots.size()) {
        return nullptr;
    }
    const GlyphSlot& slot = m_slots[id.index];
    if (!slot.active || slot.generation != id.generation) {
        return nullptr;
    }
    return &slot;
}

UIGlyphAtlas::GlyphSlot* UIGlyphAtlas::resolve(UIGlyphId id) noexcept
{
    return const_cast<GlyphSlot*>(
        static_cast<const UIGlyphAtlas*>(this)->resolve(id));
}

} // namespace Tina::UI
