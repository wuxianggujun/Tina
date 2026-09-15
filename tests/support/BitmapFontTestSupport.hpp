#pragma once

#include <tina/core/id/AssetId.hpp>
#include <tina/text/BitmapFont.hpp>

#include <memory>
#include <vector>

namespace Tina::Tests::BitmapFontFixture {

inline Text::BitmapFontDesc descriptor()
{
    return {
        .nominalSize = 8, .lineHeight = 10, .baseline = 8, .fallbackCodepoint = '?',
        .pages = {{16, 16, Text::BitmapFontImageKind::Coverage}, {16, 16, Text::BitmapFontImageKind::Color}},
        .glyphs = {
            {.codepoint = 'V', .page = 1, .width = 4, .height = 6, .advance = 5, .bearingY = 6},
            {.codepoint = ' ', .advance = 3},
            {.codepoint = 0x4E2D, .page = 1, .x = 4, .width = 7, .height = 7, .advance = 8, .bearingY = 7},
            {.codepoint = 'A', .x = 4, .width = 4, .height = 6, .advance = 5, .bearingY = 6},
            {.codepoint = '?', .width = 3, .height = 5, .advance = 4, .bearingY = 5},
        },
        .kerning = {{'A', 'V', -1}},
    };
}

inline Text::BitmapFont font()
{
    return Text::BitmapFont::Create(descriptor()).value();
}

inline std::vector<std::vector<Core::u8>> pixels()
{
    std::vector<std::vector<Core::u8>> pages(2, std::vector<Core::u8>(16 * 16 * 4));
    for (Core::usize page = 0; page < pages.size(); ++page) {
        for (Core::usize offset = 0; offset < pages[page].size(); offset += 4) {
            pages[page][offset] = page == 0 ? 200 : 80;
            pages[page][offset + 1] = page == 0 ? 100 : 160;
            pages[page][offset + 2] = page == 0 ? 50 : 240;
            pages[page][offset + 3] = 128;
        }
    }
    return pages;
}

inline std::shared_ptr<const Text::BitmapFontAtlas> atlas()
{
    return std::make_shared<const Text::BitmapFontAtlas>(Text::BitmapFontAtlas::Create(font(), pixels()).value());
}

inline Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    return Core::AssetId::fromBytes(bytes).value();
}

} // namespace Tina::Tests::BitmapFontFixture
