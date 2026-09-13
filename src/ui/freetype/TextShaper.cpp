#include "TextShaperImpl.hpp"

#include "../detail/UIGraphemeBreak.hpp"
#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIErrors.hpp>
#include <hb-ot.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace Tina::UI {
namespace {

bool weakScript(hb_script_t script) noexcept
{
    return script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED ||
           script == HB_SCRIPT_UNKNOWN;
}

bool ignorable(u32 cp) noexcept
{
    return hb_unicode_general_category(hb_unicode_funcs_get_default(), cp) ==
               HB_UNICODE_GENERAL_CATEGORY_FORMAT ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF) ||
           (cp >= 0xE0020 && cp <= 0xE007F);
}

u32 decodeValidated(std::string_view text, u32& byte) noexcept
{
    const u8 first = static_cast<u8>(text[byte++]);
    if (first < 0x80) { return first; }
    const u32 extra = first < 0xE0 ? 1U : first < 0xF0 ? 2U : 3U;
    u32 cp = first & (extra == 1 ? 0x1FU : extra == 2 ? 0x0FU : 0x07U);
    for (u32 index = 0; index < extra; ++index)
    {
        cp = (cp << 6U) | (static_cast<u8>(text[byte++]) & 0x3FU);
    }
    return cp;
}

bool shapingStyleEqual(const UITextStyle& left, const UITextStyle& right) noexcept
{
    return left.logicalSize == right.logicalSize &&
           left.lineHeightScale == right.lineHeightScale &&
           left.direction == right.direction;
}

} // namespace

TextShaper::Impl::Face::~Face()
{
    if (hb != nullptr) { hb_font_destroy(hb); }
    if (ft != nullptr) { FT_Done_Face(ft); }
}

TextShaper::Impl::Impl(UITextRasterizerCapacity config, std::pmr::memory_resource& memory)
    : capacity(config), resource(memory), faces(&memory), fallback(&memory),
      characters(&memory), byteOffsets(&memory), bidiTypes(&memory), brackets(&memory),
      levels(&memory), scripts(&memory), selectedFaces(&memory), segments(&memory),
      clusterBoundaries(&memory), glyphs(&memory), scalars(&memory), interned(&memory),
      internedText(&memory), internedGlyphs(&memory), internedScalars(&memory)
{
    faces.reserve(capacity.initialFaceCapacity);
    fallback.reserve(capacity.initialFaceCapacity);
    const usize count = capacity.maxGlyphsPerRaster;
    characters.reserve(count);
    byteOffsets.reserve(count + 1U);
    bidiTypes.reserve(count);
    brackets.reserve(count);
    levels.reserve(count);
    scripts.reserve(count);
    selectedFaces.reserve(count);
    segments.reserve(count);
    clusterBoundaries.reserve(count + 1U);
    glyphs.reserve(count);
    scalars.reserve(count);
    interned.reserve(128);
    internedText.reserve(capacity.maxTextBytes);
    const usize cachedRecords = (std::min)(count * 4U, usize{32768});
    internedGlyphs.reserve(cachedRecords);
    internedScalars.reserve(cachedRecords);
}

TextShaper::Impl::~Impl()
{
    faces.clear();
    if (buffer != nullptr) { hb_buffer_destroy(buffer); }
    if (library != nullptr) { FT_Done_FreeType(library); }
}

TextShaper::Impl::Face* TextShaper::Impl::face(UIFontFaceId id) noexcept
{
    if (!id || id.index >= faces.size() || faces[id.index].generation != id.generation)
    {
        return nullptr;
    }
    return faces[id.index].owner.get();
}

void TextShaper::Impl::invalidateCache() noexcept
{
    interned.clear();
    internedText.clear();
    internedGlyphs.clear();
    internedScalars.clear();
}

UIFontFaceId TextShaper::Impl::chooseFace(
    UIFontFaceId primary, u32 begin, u32 end) noexcept
{
    bool emoji = false;
    bool textPresentation = false;
    bool emojiSequence = false;
    for (u32 index = begin; index < end; ++index)
    {
        const u32 cp = characters[index];
        emoji = emoji || cp == 0xFE0F || (cp >= 0x1F000 && cp <= 0x1FAFF);
        textPresentation = textPresentation || cp == 0xFE0E;
        emojiSequence = emojiSequence || cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF) ||
                        (cp >= 0x1F1E6 && cp <= 0x1F1FF);
    }
    const auto covers = [&](UIFontFaceId id, bool colorOnly, bool requireSequence = false) noexcept {
        const Face* candidate = face(id);
        if (candidate == nullptr || (colorOnly && !FT_HAS_COLOR(candidate->ft))) { return false; }
        for (u32 index = begin; index < end; ++index)
        {
            const u32 cp = characters[index];
            if (ignorable(cp)) { continue; }
            hb_codepoint_t glyph = 0;
            if (!hb_font_get_nominal_glyph(candidate->hb, cp == '\t' ? ' ' : cp, &glyph))
            {
                return false;
            }
        }
        if (requireSequence)
        {
            hb_buffer_clear_contents(buffer);
            hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
            hb_buffer_set_script(buffer, HB_SCRIPT_COMMON);
            hb_buffer_set_language(buffer, hb_language_from_string("und", -1));
            hb_buffer_set_flags(buffer, HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
            hb_buffer_add_codepoints(buffer, characters.data(), static_cast<int>(characters.size()), begin, end - begin);
            hb_shape(candidate->hb, buffer, nullptr, 0);
            unsigned int count = 0;
            const auto* infos = hb_buffer_get_glyph_infos(buffer, &count);
            return hb_buffer_allocation_successful(buffer) && count == 1U && infos[0].codepoint != 0;
        }
        return true;
    };
    // A color Emoji face is selected for the WHOLE grapheme, preserving ZWJ,
    // variation selectors, modifiers and regional-indicator flag sequences.
    if (emoji && !textPresentation)
    {
        if (emojiSequence && end - begin > 1U)
        {
            if (covers(primary, true, true)) { return primary; }
            for (usize index = 0; index < fallback.size(); ++index)
            {
                if (covers(fallback[index], true, true)) { return fallback[index]; }
            }
        }
        if (covers(primary, true)) { return primary; }
        for (usize index = 0; index < fallback.size(); ++index)
        {
            if (covers(fallback[index], true)) { return fallback[index]; }
        }
    }
    if (covers(primary, false)) { return primary; }
    for (usize index = 0; index < fallback.size(); ++index)
    {
        if (covers(fallback[index], false)) { return fallback[index]; }
    }
    // .notdef is a stable, counted missing-glyph result, not an exception and
    // not a fallback for a broken/capacity-exhausted renderer.
    return primary;
}

Core::Status TextShaper::Impl::shapeLine(
    UIFontFaceId primary, std::string_view utf8, u32 beginByte, u32 endByte,
    u32 line, UITextStyle style, float& width, u32& missing)
{
    characters.clear();
    byteOffsets.clear();
    segments.clear();
    u32 byte = beginByte;
    while (byte < endByte)
    {
        byteOffsets.push_back(byte);
        characters.push_back(decodeValidated(utf8, byte));
    }
    byteOffsets.push_back(endByte);
    const u32 count = static_cast<u32>(characters.size());
    if (count == 0) { width = 0; return Core::success(); }
    // The UTF-8 byte budget bounds scalar storage. Raster/image capacity must
    // not restrict measuring a document that will be painted one row at a time.
    bidiTypes.resize(count);
    brackets.resize(count);
    levels.resize(count);
    scripts.resize(count);
    selectedFaces.resize(count);
    const usize scalarBase = scalars.size();
    scalars.resize(scalarBase + count);
    fribidi_get_bidi_types(characters.data(), static_cast<FriBidiStrIndex>(count), bidiTypes.data());
    fribidi_get_bracket_types(characters.data(), static_cast<FriBidiStrIndex>(count),
                             bidiTypes.data(), brackets.data());
    FriBidiParType baseDirection = style.direction == UITextDirection::RightToLeft ? FRIBIDI_PAR_RTL :
                                  style.direction == UITextDirection::LeftToRight ? FRIBIDI_PAR_LTR : FRIBIDI_PAR_ON;
    if (!fribidi_get_par_embedding_levels_ex(bidiTypes.data(), brackets.data(), count,
                                            &baseDirection, levels.data()) ||
        !fribidi_reorder_line(0, bidiTypes.data(), count, 0, baseDirection, levels.data(), nullptr, nullptr))
    {
        return Core::failure(UIErrorCode::InvalidText, "BiDi paragraph resolution failed");
    }
    hb_script_t previous = HB_SCRIPT_UNKNOWN;
    for (u32 index = 0; index < count; ++index)
    {
        scripts[index] = hb_unicode_script(hb_unicode_funcs_get_default(), characters[index]);
        if (weakScript(scripts[index])) { scripts[index] = previous; }
        else { previous = scripts[index]; }
    }
    hb_script_t following = HB_SCRIPT_LATIN;
    for (u32 index = count; index-- > 0;)
    {
        if (weakScript(scripts[index])) { scripts[index] = following; }
        else { following = scripts[index]; }
    }

    usize clusterByte = 0;
    u32 clusterScalar = 0;
    Detail::UIGraphemeCluster cluster{};
    const std::string_view lineText = utf8.substr(beginByte, endByte - beginByte);
    while (Detail::nextGraphemeCluster(lineText, clusterByte, clusterScalar, cluster))
    {
        const UIFontFaceId selected = chooseFace(primary, cluster.beginCodepoint, cluster.endCodepoint);
        for (u32 index = cluster.beginCodepoint; index < cluster.endCodepoint; ++index)
        {
            selectedFaces[index] = selected;
            // Marks/selectors must not split off from their base script/level.
            scripts[index] = scripts[cluster.beginCodepoint];
            levels[index] = levels[cluster.beginCodepoint];
            scalars[scalarBase + index] = UITextScalarMetrics{
                .clusterByteBegin = beginByte + static_cast<u32>(cluster.beginByte),
                .clusterByteEnd = beginByte + static_cast<u32>(cluster.endByte),
                .line = line,
                .rightToLeft = (levels[index] & 1U) != 0,
                .paragraphRightToLeft = FRIBIDI_IS_RTL(baseDirection) != 0,
                .hasVisualPosition = true,
            };
        }
    }
    for (u32 begin = 0; begin < count;)
    {
        u32 end = begin + 1U;
        while (end < count && selectedFaces[end] == selectedFaces[begin] &&
               scripts[end] == scripts[begin] && levels[end] == levels[begin]) { ++end; }
        segments.push_back({begin, end, selectedFaces[begin], scripts[begin], levels[begin]});
        begin = end;
    }
    // UAX #9 L2 reorders complete shaping runs, not bytes or glyphs inside a
    // run. HarfBuzz already produces the correct internal RTL glyph order.
    int maximumLevel = 0;
    int minimumOddLevel = 256;
    for (const Segment& segment : segments)
    {
        maximumLevel = (std::max)(maximumLevel, static_cast<int>(segment.level));
        if (segment.level & 1U) { minimumOddLevel = (std::min)(minimumOddLevel, static_cast<int>(segment.level)); }
    }
    for (int level = maximumLevel; level >= minimumOddLevel; --level)
    {
        usize begin = 0;
        while (begin < segments.size())
        {
            while (begin < segments.size() && segments[begin].level < level) { ++begin; }
            usize end = begin;
            while (end < segments.size() && segments[end].level >= level) { ++end; }
            std::reverse(segments.begin() + begin, segments.begin() + end);
            begin = end;
        }
    }

    float penX = 0.0F;
    const float lineY = static_cast<float>(line) * style.logicalSize * style.lineHeightScale;
    for (const Segment& segment : segments)
    {
        Face* selected = face(segment.face);
        hb_buffer_clear_contents(buffer);
        hb_buffer_set_direction(buffer, (segment.level & 1U) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_set_script(buffer, segment.script);
        hb_buffer_set_language(buffer, hb_language_from_string("und", -1));
        hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
        hb_buffer_set_flags(buffer, static_cast<hb_buffer_flags_t>(
            HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES |
            (segment.begin == 0 ? HB_BUFFER_FLAG_BOT : 0) |
            (segment.end == count ? HB_BUFFER_FLAG_EOT : 0)));
        // Preserve surrounding context across script/font runs, while keeping
        // original UTF-8 byte offsets in the shaping clusters.
        hb_buffer_add_utf8(buffer, utf8.data(), static_cast<int>(utf8.size()),
                           byteOffsets[segment.begin],
                           byteOffsets[segment.end] - byteOffsets[segment.begin]);
        unsigned int unicodeCount = 0;
        hb_glyph_info_t* unicode = hb_buffer_get_glyph_infos(buffer, &unicodeCount);
        for (u32 index = 0; index < unicodeCount; ++index)
        {
            if (unicode[index].codepoint == '\t') { unicode[index].codepoint = ' '; }
        }
        hb_shape(selected->hb, buffer, nullptr, 0);
        unsigned int length = 0;
        const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &length);
        const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, nullptr);
        if (!hb_buffer_allocation_successful(buffer))
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "HarfBuzz shaping allocation failed");
        }
        if (length > (std::numeric_limits<u32>::max)() - glyphs.size())
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "Shaped glyph indices exceed their addressable range");
        }
        clusterBoundaries.clear();
        for (u32 index = 0; index < length; ++index) { clusterBoundaries.push_back(infos[index].cluster); }
        clusterBoundaries.push_back(byteOffsets[segment.end]);
        std::sort(clusterBoundaries.begin(), clusterBoundaries.end());
        clusterBoundaries.erase(std::unique(clusterBoundaries.begin(), clusterBoundaries.end()), clusterBoundaries.end());
        const float units = style.logicalSize / static_cast<float>(selected->unitsPerEm);
        for (u32 first = 0; first < length;)
        {
            u32 last = first + 1U;
            while (last < length && infos[last].cluster == infos[first].cluster) { ++last; }
            const u32 clusterBegin = infos[first].cluster;
            const u32 clusterEnd = *std::upper_bound(clusterBoundaries.begin(), clusterBoundaries.end(), clusterBegin);
            const u32 scalarBegin = static_cast<u32>(std::lower_bound(byteOffsets.begin(), byteOffsets.end(), clusterBegin) - byteOffsets.begin());
            const u32 scalarEnd = static_cast<u32>(std::lower_bound(byteOffsets.begin(), byteOffsets.end(), clusterEnd) - byteOffsets.begin());
            const float clusterX = penX;
            for (u32 index = first; index < last; ++index)
            {
                const float tabScale = scalarBegin < count && characters[scalarBegin] == '\t' ? 4.0F : 1.0F;
                const float advanceX = static_cast<float>(positions[index].x_advance) * units * tabScale;
                const float offsetX = static_cast<float>(positions[index].x_offset) * units;
                const float offsetY = -static_cast<float>(positions[index].y_offset) * units;
                glyphs.push_back(ShapedGlyph{
                    .face = segment.face, .glyphIndex = infos[index].codepoint,
                    .clusterByteBegin = clusterBegin, .clusterByteEnd = clusterEnd, .line = line,
                    .x = penX + offsetX, .y = lineY + offsetY,
                    .advanceX = advanceX,
                    .advanceY = -static_cast<float>(positions[index].y_advance) * units,
                    .offsetX = offsetX, .offsetY = offsetY,
                });
                if (infos[index].codepoint == 0) { ++missing; }
                penX += advanceX;
            }
            const u32 scalarCount = scalarEnd - scalarBegin;
            const float clusterAdvance = (std::max)(0.0F, penX - clusterX);
            for (u32 index = scalarBegin; index < scalarEnd; ++index)
            {
                const float share = clusterAdvance / static_cast<float>(scalarCount);
                const float before = share * static_cast<float>(index - scalarBegin);
                const bool rtl = (segment.level & 1U) != 0;
                scalars[scalarBase + index] = UITextScalarMetrics{
                    .advance = share,
                    .visualStartX = rtl ? penX - before : clusterX + before,
                    .visualEndX = rtl ? penX - before - share : clusterX + before + share,
                    .clusterByteBegin = clusterBegin, .clusterByteEnd = clusterEnd,
                    .line = line, .rightToLeft = rtl,
                    .paragraphRightToLeft = FRIBIDI_IS_RTL(baseDirection) != 0,
                    .hasVisualPosition = true,
                };
            }
            first = last;
        }
    }
    if (!std::isfinite(penX) || penX < 0.0F)
    {
        return Core::failure(UIErrorCode::InvalidText, "Shaped text metrics are not finite");
    }
    width = penX;
    return Core::success();
}

Core::Result<GlyphRun> TextShaper::Impl::shape(
    UIFontFaceId primary, std::string_view utf8, UITextStyle style)
{
    Face* primaryFace = face(primary);
    if (primaryFace == nullptr) { return Core::failure(UIErrorCode::InvalidFont, "Shaping requires a live font face"); }
    if (!std::isfinite(style.logicalSize) || style.logicalSize <= 0.0F ||
        !std::isfinite(style.lineHeightScale) || style.lineHeightScale <= 0.0F ||
        !std::isfinite(style.advanceScale) || style.advanceScale <= 0.0F ||
        style.direction > UITextDirection::RightToLeft ||
        style.pixelSnap > UITextPixelSnap::RunOrigin ||
        !std::isfinite(style.logicalSize * style.lineHeightScale) ||
        !Core::isStrictUtf8WithoutNul(utf8))
    {
        return Core::failure(UIErrorCode::InvalidText, "Text must be strict UTF-8 with finite positive style metrics");
    }
    if (utf8.size() > capacity.maxTextBytes)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "Text shaping UTF-8 byte capacity exhausted");
    }
    for (const InternedRun& cached : interned)
    {
        if (cached.primary == primary && shapingStyleEqual(cached.style, style) &&
            std::string_view(internedText.data() + cached.textBegin, cached.textLength) == utf8)
        {
            return GlyphRun{cached.metrics, cached.baseline,
                std::span<const ShapedGlyph>(internedGlyphs).subspan(cached.glyphBegin, cached.glyphCount),
                std::span<const UITextScalarMetrics>(internedScalars).subspan(cached.scalarBegin, cached.scalarCount),
                cached.missing};
        }
    }
    glyphs.clear();
    scalars.clear();
    u32 begin = 0;
    u32 lineCount = 0;
    u32 missing = 0;
    float widest = 0.0F;
    if (!utf8.empty())
    {
        do
        {
            const usize delimiter = utf8.find('\n', begin);
            const u32 end = static_cast<u32>(delimiter == std::string_view::npos ? utf8.size() : delimiter);
            float width = 0;
            if (auto status = shapeLine(primary, utf8, begin, end, lineCount, style, width, missing); !status)
            {
                return Core::failure(status.error());
            }
            widest = (std::max)(widest, width);
            ++lineCount;
            begin = end + 1U;
        } while (begin <= utf8.size());
    }
    const float lineHeight = style.logicalSize * style.lineHeightScale;
    const float upem = static_cast<float>(primaryFace->unitsPerEm);
    hb_font_extents_t extents{};
    const bool hasExtents = hb_font_get_h_extents(primaryFace->hb, &extents);
    const float ascent = static_cast<float>(hasExtents ? extents.ascender : primaryFace->ft->ascender) * style.logicalSize / upem;
    const float descent = -static_cast<float>(hasExtents ? extents.descender : primaryFace->ft->descender) * style.logicalSize / upem;
    const float baseline = std::clamp(ascent + (lineHeight - ascent - descent) * 0.5F, 0.0F, lineHeight);
    const UITextMetrics metrics{{widest, lineHeight * static_cast<float>(lineCount)},
                                static_cast<u32>(scalars.size()) + (lineCount == 0 ? 0U : lineCount - 1U), lineCount};
    if (!std::isfinite(metrics.measuredSize.height))
    {
        return Core::failure(UIErrorCode::InvalidText, "Shaped line height overflowed");
    }
    if (interned.size() == interned.capacity() || utf8.size() > internedText.capacity() - internedText.size() ||
        glyphs.size() > internedGlyphs.capacity() - internedGlyphs.size() ||
        scalars.size() > internedScalars.capacity() - internedScalars.size()) { invalidateCache(); }
    const InternedRun cached{primary, style,
        static_cast<u32>(internedText.size()), static_cast<u32>(utf8.size()),
        static_cast<u32>(internedGlyphs.size()), static_cast<u32>(glyphs.size()),
        static_cast<u32>(internedScalars.size()), static_cast<u32>(scalars.size()), metrics, baseline, missing};
    if (glyphs.size() <= internedGlyphs.capacity() && scalars.size() <= internedScalars.capacity())
    {
        internedText.insert(internedText.end(), utf8.begin(), utf8.end());
        internedGlyphs.insert(internedGlyphs.end(), glyphs.begin(), glyphs.end());
        internedScalars.insert(internedScalars.end(), scalars.begin(), scalars.end());
        interned.push_back(cached);
    }
    return GlyphRun{metrics, baseline, glyphs, scalars, missing};
}

TextShaper::TextShaper(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}
TextShaper::~TextShaper() = default;

Core::Result<std::unique_ptr<TextShaper>> TextShaper::Create(
    UITextRasterizerCapacity capacity, std::pmr::memory_resource& resource)
{
    if (auto status = validateUITextRasterizerCapacity(capacity); !status) { return Core::failure(status.error()); }
    try
    {
        auto impl = std::make_unique<Impl>(capacity, resource);
        if (FT_Init_FreeType(&impl->library) != 0)
        {
            return Core::failure(UIErrorCode::InvalidFont, "Font library initialization failed");
        }
        impl->buffer = hb_buffer_create();
        if (!hb_buffer_allocation_successful(impl->buffer) ||
            !hb_buffer_pre_allocate(impl->buffer, capacity.maxGlyphsPerRaster))
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "Shaping buffer allocation failed");
        }
        return std::unique_ptr<TextShaper>(new TextShaper(std::move(impl)));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Text shaper allocation failed");
    }
}

Core::Result<UIFontFaceId> TextShaper::openFace(std::span<const std::byte> bytes, i32 faceIndex)
{
    if (bytes.empty() || bytes.size() > m_impl->capacity.maxFontBytes ||
        bytes.size() > static_cast<usize>((std::numeric_limits<FT_Long>::max)()) || faceIndex < 0)
    {
        return Core::failure(UIErrorCode::InvalidFont, "Font byte length or face index is invalid");
    }
    try
    {
        usize index = 0;
        for (; index < m_impl->faces.size(); ++index)
        {
            const auto& slot = m_impl->faces[index];
            if (!slot.owner && slot.generation != (std::numeric_limits<u32>::max)()) { break; }
        }
        if (index == (std::numeric_limits<u32>::max)())
        { return Core::failure(UIErrorCode::CapacityExceeded, "Font face identity space exhausted"); }
        auto opened = std::make_unique<Impl::Face>(m_impl->resource);
        opened->bytes.assign(bytes.begin(), bytes.end());
        opened->faceIndex = faceIndex;
        if (FT_New_Memory_Face(m_impl->library, reinterpret_cast<const FT_Byte*>(opened->bytes.data()),
                              static_cast<FT_Long>(opened->bytes.size()), faceIndex, &opened->ft) != 0 ||
            FT_Select_Charmap(opened->ft, FT_ENCODING_UNICODE) != 0)
        {
            return Core::failure(UIErrorCode::InvalidFont, "Unable to open a Unicode font face");
        }
        hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(opened->bytes.data()),
                                       static_cast<unsigned int>(opened->bytes.size()),
                                       HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        hb_face_t* hbFace = hb_face_create(blob, static_cast<unsigned int>(faceIndex));
        hb_blob_destroy(blob);
        opened->unitsPerEm = hb_face_get_upem(hbFace);
        opened->hb = hb_font_create(hbFace);
        const unsigned int glyphCount = hb_face_get_glyph_count(hbFace);
        hb_face_destroy(hbFace);
        if (opened->unitsPerEm == 0 || glyphCount == 0 || opened->hb == hb_font_get_empty())
        {
            return Core::failure(UIErrorCode::InvalidFont, "Font has no usable OpenType glyph data");
        }
        hb_ot_font_set_funcs(opened->hb);
        hb_font_set_scale(opened->hb, opened->unitsPerEm, opened->unitsPerEm);
        if (index == m_impl->faces.size()) { m_impl->faces.emplace_back(); }
        auto& slot = m_impl->faces[index];
        slot.owner = std::move(opened);
        ++slot.generation;
        m_impl->invalidateCache();
        return UIFontFaceId{static_cast<u32>(index), slot.generation};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Font face allocation failed");
    }
    catch (const std::length_error&)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "Font face storage exceeds addressable size");
    }
}

Core::Status TextShaper::closeFace(UIFontFaceId face) noexcept
{
    if (m_impl->face(face) == nullptr) { return Core::failure(UIErrorCode::InvalidFont, "Font face is stale or closed"); }
    m_impl->faces[face.index].owner.reset();
    m_impl->invalidateCache();
    std::erase(m_impl->fallback, face);
    return Core::success();
}

Core::Status TextShaper::setFallbackChain(std::span<const UIFontFaceId> faces)
{
    for (usize index = 0; index < faces.size(); ++index)
    {
        if (m_impl->face(faces[index]) == nullptr ||
            std::find(faces.begin(), faces.begin() + index, faces[index]) != faces.begin() + index)
        {
            return Core::failure(UIErrorCode::InvalidFont, "Font fallback chain contains a stale or duplicate face");
        }
    }
    try { m_impl->fallback.assign(faces.begin(), faces.end()); }
    catch (const std::bad_alloc&)
    { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Font fallback chain allocation failed"); }
    catch (const std::length_error&)
    { return Core::failure(UIErrorCode::CapacityExceeded, "Font fallback chain exceeds addressable size"); }
    m_impl->invalidateCache();
    return Core::success();
}

Core::Result<GlyphRun> TextShaper::shape(UIFontFaceId primary, std::string_view utf8, UITextStyle style)
{
    try { return m_impl->shape(primary, utf8, style); }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Text shaping allocation failed");
    }
    catch (const std::length_error&)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "Text shaping exceeds addressable storage");
    }
}

} // namespace Tina::UI
