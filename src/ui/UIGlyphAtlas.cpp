#include <tina/ui/text/UIGlyphAtlas.hpp>
#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <new>

namespace Tina::UI {
namespace {
constexpr u32 EmptySlot = (std::numeric_limits<u32>::max)();
constexpr u32 Gutter = 1;

usize hashKey(const UIGlyphKey& key) noexcept
{
    u64 hash = key.face.generation;
    for (u32 value : {key.face.index, key.glyphIndex, key.rasterSize.x,
                       key.rasterSize.y, static_cast<u32>(key.imageKind)})
    {
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    }
    return static_cast<usize>(hash);
}
}

Core::Status validateUIGlyphAtlasCapacity(const UIGlyphAtlasCapacity& capacity)
{
    if (capacity.width == 0 || capacity.height == 0 || capacity.maxGlyphs == 0 ||
        capacity.width > UIGlyphAtlasCapacity::MaxWidth ||
        capacity.height > UIGlyphAtlasCapacity::MaxHeight ||
        capacity.maxGlyphs > UIGlyphAtlasCapacity::MaxGlyphs)
    {
        return Core::failure(UIErrorCode::InvalidContextConfig, "Invalid bounded UI glyph atlas capacity");
    }
    return Core::success();
}

UIGlyphAtlas::UIGlyphAtlas(UIGlyphAtlasCapacity capacity, std::pmr::memory_resource& resource)
    : m_capacity(capacity), m_slots(&resource), m_freeSlots(&resource),
      m_shelves(&resource), m_page(&resource), m_lookup(&resource)
{
    m_slots.resize(capacity.maxGlyphs);
    m_freeSlots.reserve(capacity.maxGlyphs);
    for (u32 index = capacity.maxGlyphs; index > 0; --index) { m_freeSlots.push_back(index - 1); }
    m_shelves.reserve(capacity.height);
    m_page.resize(static_cast<usize>(capacity.width) * capacity.height * 4U, 0);
    m_lookup.resize(std::bit_ceil(static_cast<usize>(capacity.maxGlyphs) * 2U), EmptySlot);
}

Core::Result<std::unique_ptr<UIGlyphAtlas>> UIGlyphAtlas::Create(
    UIGlyphAtlasCapacity capacity, std::pmr::memory_resource& resource)
{
    if (auto status = validateUIGlyphAtlasCapacity(capacity); !status) { return Core::failure(status.error()); }
    try { return std::unique_ptr<UIGlyphAtlas>(new UIGlyphAtlas(capacity, resource)); }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "UI glyph atlas allocation failed");
    }
}

Core::Result<UIGlyphPlacement> UIGlyphAtlas::insert(
    const UIGlyphKey& key, const UITextGlyphRaster& glyph, std::span<const u8> coverage)
{
    if (!key.face || key.rasterSize.x == 0 || key.rasterSize.y == 0 ||
        key.imageKind > UIGlyphImageKind::Color)
    {
        return Core::failure(UIErrorCode::InvalidFont, "Invalid shaped glyph cache key");
    }
    const u64 required = static_cast<u64>(glyph.width) * glyph.height * 4U;
    if (required != 0 && (glyph.coveragePitch != static_cast<u64>(glyph.width) * 4U || coverage.size() < required))
    {
        return Core::failure(UIErrorCode::InvalidText, "Glyph pixels must be tightly packed RGBA8");
    }
    if (auto existing = find(key); existing) { return *existing; }
    return placeNew(key, glyph, coverage);
}

Core::Result<UIGlyphPlacement> UIGlyphAtlas::placeNew(
    const UIGlyphKey& key, const UITextGlyphRaster& glyph, std::span<const u8> coverage)
{
    if (m_freeSlots.empty() || m_slots[m_freeSlots.back()].generation == EmptySlot)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI glyph atlas slot capacity exhausted");
    }
    u32 atlasX = 0;
    u32 atlasY = 0;
    if (glyph.width != 0 && glyph.height != 0)
    {
        if (m_capacity.width <= 2U * Gutter || m_capacity.height <= 2U * Gutter ||
            glyph.width > m_capacity.width - 2U * Gutter || glyph.height > m_capacity.height - 2U * Gutter)
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "Glyph plus filter gutter exceeds atlas extent");
        }
        const u32 packedWidth = glyph.width + 2U * Gutter;
        const u32 packedHeight = glyph.height + 2U * Gutter;
        Shelf* target = nullptr;
        for (Shelf& shelf : m_shelves)
        {
            if (packedHeight <= shelf.height && packedWidth <= m_capacity.width - shelf.x)
            {
                target = &shelf;
                break;
            }
        }
        if (target == nullptr)
        {
            if (packedHeight > m_capacity.height - m_nextShelfY)
            {
                return Core::failure(UIErrorCode::CapacityExceeded, "UI glyph atlas pixel budget exhausted");
            }
            m_shelves.push_back(Shelf{m_nextShelfY, packedHeight, 0});
            m_nextShelfY += packedHeight;
            target = &m_shelves.back();
        }
        atlasX = target->x + Gutter;
        atlasY = target->y + Gutter;
        target->x += packedWidth;
        for (u32 row = 0; row < glyph.height; ++row)
        {
            std::memcpy(m_page.data() + (static_cast<usize>(atlasY + row) * m_capacity.width + atlasX) * 4U,
                        coverage.data() + static_cast<usize>(row) * glyph.coveragePitch, glyph.coveragePitch);
        }
        ++m_pageRevision;
    }
    const u32 index = m_freeSlots.back();
    m_freeSlots.pop_back();
    GlyphSlot& slot = m_slots[index];
    ++slot.generation;
    slot.key = key;
    slot.atlasX = atlasX;
    slot.atlasY = atlasY;
    slot.width = glyph.width;
    slot.height = glyph.height;
    slot.active = true;
    usize bucket = hashKey(key) & (m_lookup.size() - 1U);
    while (m_lookup[bucket] != EmptySlot) { bucket = (bucket + 1U) & (m_lookup.size() - 1U); }
    m_lookup[bucket] = index;
    ++m_glyphCount;
    m_glyphHighWater = (std::max)(m_glyphHighWater, m_glyphCount);
    m_usedPixels += glyph.width * glyph.height;
    m_usedPixelHighWater = (std::max)(m_usedPixelHighWater, m_usedPixels);
    return UIGlyphPlacement{{index, slot.generation}, key, atlasX, atlasY, glyph.width, glyph.height};
}

Core::Result<UIGlyphPlacement> UIGlyphAtlas::find(const UIGlyphKey& key) const noexcept
{
    usize bucket = hashKey(key) & (m_lookup.size() - 1U);
    for (usize probe = 0; probe < m_lookup.size(); ++probe)
    {
        const u32 index = m_lookup[bucket];
        if (index == EmptySlot) { break; }
        const GlyphSlot& slot = m_slots[index];
        if (slot.key == key)
        {
            return UIGlyphPlacement{{index, slot.generation}, key, slot.atlasX, slot.atlasY, slot.width, slot.height};
        }
        bucket = (bucket + 1U) & (m_lookup.size() - 1U);
    }
    return Core::failure(UIErrorCode::InvalidNode, "Glyph is not resident in the atlas");
}

bool UIGlyphAtlas::contains(UIGlyphId id) const noexcept { return resolve(id) != nullptr; }

void UIGlyphAtlas::clear() noexcept
{
    for (GlyphSlot& slot : m_slots) { slot.active = false; }
    m_freeSlots.clear();
    for (u32 index = static_cast<u32>(m_slots.size()); index > 0; --index) { m_freeSlots.push_back(index - 1); }
    m_shelves.clear();
    std::fill(m_lookup.begin(), m_lookup.end(), EmptySlot);
    std::fill(m_page.begin(), m_page.end(), 0);
    m_nextShelfY = m_glyphCount = m_usedPixels = 0;
    ++m_pageRevision;
}

UIGlyphAtlasCapacity UIGlyphAtlas::capacity() const noexcept { return m_capacity; }
UIGlyphAtlasStatistics UIGlyphAtlas::statistics() const noexcept
{
    return {m_capacity.width, m_capacity.height, m_capacity.maxGlyphs, m_glyphCount,
            m_glyphHighWater, m_usedPixels, m_usedPixelHighWater, static_cast<u32>(m_shelves.size())};
}
std::span<const u8> UIGlyphAtlas::pagePixels() const noexcept { return m_page; }
u64 UIGlyphAtlas::pageRevision() const noexcept { return m_pageRevision; }
const UIGlyphAtlas::GlyphSlot* UIGlyphAtlas::resolve(UIGlyphId id) const noexcept
{
    if (!id || id.index >= m_slots.size()) { return nullptr; }
    const GlyphSlot& slot = m_slots[id.index];
    return slot.active && slot.generation == id.generation ? &slot : nullptr;
}
UIGlyphAtlas::GlyphSlot* UIGlyphAtlas::resolve(UIGlyphId id) noexcept
{
    return const_cast<GlyphSlot*>(static_cast<const UIGlyphAtlas*>(this)->resolve(id));
}
} // namespace Tina::UI
