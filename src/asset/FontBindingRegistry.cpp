#include <tina/asset/FontBindingRegistry.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormat.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::Status requireFontHandle(AssetSystem& assets, AssetHandle handle) noexcept
{
    if (!handle)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "Font handle is empty");
    }
    const CookedAssetFile* file = assets.tryGet(handle);
    if (file == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "Font asset is not resident");
    }
    if (file->header().assetKind != AssetFormat::AssetKind::Font)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "Asset is not a Font");
    }
    return Core::success();
}

} // namespace

Core::Result<FontBindingRegistry> FontBindingRegistry::Create(
    AssetSystem& assets, FontBindingRegistryConfig config)
{
    std::pmr::memory_resource* memory = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();
    FontBindingRegistry registry{&assets, memory};
    try
    {
        registry.records_.reserve(config.initialFontReserve);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "FontBindingRegistry storage allocation failed");
    }
    return registry;
}

FontBindingRegistry::Record* FontBindingRegistry::find(AssetHandle handle) noexcept
{
    const auto found = std::ranges::find(records_, handle, &Record::handle);
    return found == records_.end() ? nullptr : &*found;
}

const FontBindingRegistry::Record* FontBindingRegistry::find(AssetHandle handle) const noexcept
{
    const auto found = std::ranges::find(records_, handle, &Record::handle);
    return found == records_.end() ? nullptr : &*found;
}

Core::Result<std::shared_ptr<const Text::BitmapFont>> FontBindingRegistry::intern(AssetHandle fontAsset)
{
    if (assets_ == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "FontBindingRegistry was not created");
    }
    if (Record* existing = find(fontAsset); existing != nullptr)
    {
        return existing->font;
    }
    if (Core::Status status = requireFontHandle(*assets_, fontAsset); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto lease = assets_->acquire(fontAsset);
    if (!lease)
    {
        return Core::failure(std::move(lease.error()));
    }
    const CookedAssetFile* file = lease->get();
    if (file == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "Font lease lost its cooked payload");
    }
    auto parsed = parseBitmapFontFromCooked(*file);
    if (!parsed)
    {
        return Core::failure(std::move(parsed.error()));
    }
    try
    {
        Record record{};
        record.handle = fontAsset;
        record.lease = std::move(*lease);
        record.pages = std::move(parsed->textureIds);
        record.font = std::make_shared<const Text::BitmapFont>(std::move(parsed->font));
        records_.push_back(std::move(record));
        return records_.back().font;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "Font intern allocation failed");
    }
}

Core::Result<std::shared_ptr<const Text::BitmapFontAtlas>> FontBindingRegistry::internAtlas(AssetHandle fontAsset)
{
    auto interned = intern(fontAsset);
    if (!interned)
    {
        return Core::failure(std::move(interned.error()));
    }
    Record* record = find(fontAsset);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "Font intern record disappeared");
    }
    if (record->atlas)
    {
        return record->atlas;
    }
    std::vector<const CookedAssetFile*> pages;
    try
    {
        pages.reserve(record->pages.size());
        record->pageLeases.clear();
        record->pageLeases.reserve(record->pages.size());
        for (const Core::AssetId pageId : record->pages)
        {
            auto handle = assets_->find(pageId);
            if (!handle)
            {
                return Core::failure(AssetErrorCode::AssetNotReady, "Font page texture is not loaded");
            }
            auto pageLease = assets_->acquire(*handle);
            if (!pageLease)
            {
                return Core::failure(std::move(pageLease.error()));
            }
            const CookedAssetFile* pageFile = pageLease->get();
            if (pageFile == nullptr)
            {
                return Core::failure(AssetErrorCode::AssetNotReady, "Font page lease lost its cooked payload");
            }
            pages.push_back(pageFile);
            record->pageLeases.push_back(std::move(*pageLease));
        }
        auto atlas = loadBitmapFontAtlasFromCooked(*record->lease.get(), pages);
        if (!atlas)
        {
            record->pageLeases.clear();
            return Core::failure(std::move(atlas.error()));
        }
        record->atlas = std::make_shared<const Text::BitmapFontAtlas>(std::move(*atlas));
        return record->atlas;
    }
    catch (const std::bad_alloc&)
    {
        record->pageLeases.clear();
        record->atlas.reset();
        return Core::failure(AssetErrorCode::AllocationFailed, "Font atlas intern allocation failed");
    }
}

const Text::BitmapFont* FontBindingRegistry::font(AssetHandle fontAsset) const noexcept
{
    const Record* record = find(fontAsset);
    return record != nullptr ? record->font.get() : nullptr;
}

std::span<const Core::AssetId> FontBindingRegistry::pages(AssetHandle fontAsset) const noexcept
{
    const Record* record = find(fontAsset);
    if (record == nullptr)
    {
        return {};
    }
    return record->pages;
}

Core::usize FontBindingRegistry::internedCount() const noexcept
{
    return records_.size();
}

} // namespace Tina::Asset
