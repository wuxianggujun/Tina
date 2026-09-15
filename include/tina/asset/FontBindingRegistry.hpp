#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/text/BitmapFont.hpp>

#include <memory>
#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Asset {

struct FontBindingRegistryConfig final {
    Core::usize initialFontReserve = 8;
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Owner-thread intern table for cooked Font assets. Metrics and optional CPU
// atlases are shared; page textures stay ordinary Texture2D identities for the
// Sprite2D binding path. This is the product Font owner: UI and world text both
// hold the interned shared_ptr instead of parsing a second copy.
class FontBindingRegistry final {
  public:
    [[nodiscard]] static Core::Result<FontBindingRegistry> Create(
        AssetSystem& assets, FontBindingRegistryConfig config = {});

    ~FontBindingRegistry() noexcept = default;
    FontBindingRegistry(const FontBindingRegistry&) = delete;
    FontBindingRegistry& operator=(const FontBindingRegistry&) = delete;
    FontBindingRegistry(FontBindingRegistry&&) noexcept = default;
    FontBindingRegistry& operator=(FontBindingRegistry&&) = delete;

    [[nodiscard]] Core::Result<std::shared_ptr<const Text::BitmapFont>> intern(AssetHandle fontAsset);
    [[nodiscard]] Core::Result<std::shared_ptr<const Text::BitmapFontAtlas>> internAtlas(AssetHandle fontAsset);
    [[nodiscard]] const Text::BitmapFont* font(AssetHandle fontAsset) const noexcept;
    [[nodiscard]] std::span<const Core::AssetId> pages(AssetHandle fontAsset) const noexcept;
    [[nodiscard]] Core::usize internedCount() const noexcept;

  private:
    struct Record final {
        AssetHandle handle{};
        AssetLease lease{};
        std::vector<AssetLease> pageLeases{};
        std::shared_ptr<const Text::BitmapFont> font{};
        std::vector<Core::AssetId> pages{};
        std::shared_ptr<const Text::BitmapFontAtlas> atlas{};
    };

    explicit FontBindingRegistry(AssetSystem* assets, std::pmr::memory_resource* memory)
        : assets_(assets), memory_(memory)
    {
    }

    [[nodiscard]] Record* find(AssetHandle handle) noexcept;
    [[nodiscard]] const Record* find(AssetHandle handle) const noexcept;

    AssetSystem* assets_ = nullptr;
    std::pmr::memory_resource* memory_ = nullptr;
    std::vector<Record> records_{};
};

} // namespace Tina::Asset
