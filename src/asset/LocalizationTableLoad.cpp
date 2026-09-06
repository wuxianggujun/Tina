#include <tina/asset/LocalizationTableLoad.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <new>
#include <utility>
#include <vector>

namespace Tina::Asset {

Core::Result<Localization::LocalizationCatalog> loadLocalizationCatalogFromPayloadView(
    const AssetFormat::LocalizationTablePayloadView& view,
    LocalizationCatalogLoadConfig config,
    std::pmr::memory_resource& resource)
{
    const Localization::LocaleTag locale = Localization::LocaleTag::parse(view.localeTag);
    if (!locale.hasValue())
    {
        // The wire accepts any NUL-padded byte run in the tag field, while LocaleTag accepts only
        // the ASCII BCP 47 shape. A tag that parses on one side and not the other is a real
        // mismatch, so it is reported rather than silently dropped.
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "cooked localization locale tag is not a valid BCP 47 subset tag");
    }

    std::vector<Localization::LocalizationEntryDesc> entries;
    try
    {
        entries.reserve(view.entryCount);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "localization entry staging allocation failed");
    }
    for (Core::u32 index = 0; index < view.entryCount; ++index)
    {
        const auto entry = view.entry(index);
        if (!entry)
        {
            // A parsed view guarantees every index in [0, entryCount) decodes, so reaching this is
            // an internal inconsistency rather than a bad file.
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "cooked localization entry index is not decodable");
        }
        entries.push_back(Localization::LocalizationEntryDesc{
            .keyHash = entry->keyHash,
            .textOffset = entry->textOffset,
            .textLength = entry->textLength,
        });
    }

    // Zero means "size to this table exactly". Capacity is still validated by Create(), so an
    // explicit value smaller than the table fails closed instead of being widened here.
    const Core::u32 entryCapacity =
        config.entryCapacity != 0U ? config.entryCapacity : view.entryCount;
    const Core::usize textByteCapacity =
        config.textByteCapacity != 0U ? config.textByteCapacity : view.textBlob.size();

    return Localization::LocalizationCatalog::Create(
        Localization::LocalizationTableDesc{
            .entries = entries,
            .text = std::span<const char>(view.textBlob.data(), view.textBlob.size()),
            .locale = locale,
        },
        Localization::LocalizationCatalogConfig{
            .entryCapacity = entryCapacity,
            .textByteCapacity = textByteCapacity,
        },
        resource);
}

Core::Result<Localization::LocalizationCatalog> loadLocalizationCatalogFromPayload(
    std::span<const std::byte> payload,
    LocalizationCatalogLoadConfig config,
    std::pmr::memory_resource& resource)
{
    auto view = AssetFormat::parseLocalizationTablePayload(payload);
    if (!view)
    {
        return Core::failure(std::move(view.error()));
    }
    return loadLocalizationCatalogFromPayloadView(*view, config, resource);
}

} // namespace Tina::Asset
