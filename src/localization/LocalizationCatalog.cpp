#include <tina/localization/LocalizationCatalog.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/localization/LocalizationErrors.hpp>

#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace Tina::Localization {
namespace {

// Process-wide, never recycled. A recycled identity would let an id outliving its catalog resolve
// against a later one, which is exactly the silent cross-catalog hit the identity exists to stop.
[[nodiscard]] std::optional<Core::u32> createUniqueCatalogIdentity() noexcept
{
    static std::atomic<Core::u64> nextIdentity{1};
    constexpr Core::u64 MaximumIdentity = (std::numeric_limits<Core::u32>::max)();

    Core::u64 candidate = nextIdentity.load(std::memory_order_relaxed);
    while (candidate <= MaximumIdentity) {
        if (nextIdentity.compare_exchange_weak(candidate,
                                               candidate + 1,
                                               std::memory_order_relaxed,
                                               std::memory_order_relaxed)) {
            return static_cast<Core::u32>(candidate);
        }
    }
    return std::nullopt;
}

} // namespace

LocalizationCatalog::LocalizationCatalog(Core::u32 identity,
                                         LocaleTag locale,
                                         std::pmr::vector<Slot> entries,
                                         std::pmr::vector<char> text,
                                         Core::u32 entryCapacity,
                                         Core::usize textByteCapacity) noexcept
    : m_identity(identity),
      m_locale(locale),
      m_entries(std::move(entries)),
      m_text(std::move(text)),
      m_entryCapacity(entryCapacity),
      m_textByteCapacity(textByteCapacity)
{
}

LocalizationCatalog::LocalizationCatalog(LocalizationCatalog&& other) noexcept
    : m_identity(std::exchange(other.m_identity, 0)),
      m_locale(std::exchange(other.m_locale, LocaleTag{})),
      m_entries(std::move(other.m_entries)),
      m_text(std::move(other.m_text)),
      m_entryCapacity(std::exchange(other.m_entryCapacity, 0)),
      m_textByteCapacity(std::exchange(other.m_textByteCapacity, 0)),
      m_revision(std::exchange(other.m_revision, 0))
{
}

Core::Result<LocalizationCatalog> LocalizationCatalog::Create(const LocalizationTableDesc& desc,
                                                             LocalizationCatalogConfig config,
                                                             std::pmr::memory_resource& resource)
{
    if (config.entryCapacity == 0U
        || config.entryCapacity > LocalizationCatalogContract::MaximumEntryCount) {
        return Core::failure(LocalizationErrorCode::CapacityExceeded,
                             "localization entry capacity is outside the supported range");
    }
    if (config.textByteCapacity > LocalizationCatalogContract::MaximumTextBytes) {
        return Core::failure(LocalizationErrorCode::CapacityExceeded,
                             "localization text capacity is outside the supported range");
    }
    if (!desc.locale.hasValue()) {
        return Core::failure(LocalizationErrorCode::InvalidLocaleTag,
                             "localization table requires a valid locale tag");
    }
    if (desc.entries.empty()) {
        return Core::failure(LocalizationErrorCode::InvalidData,
                             "localization table requires at least one entry");
    }
    if (desc.entries.size() > LocalizationCatalogContract::MaximumEntryCount) {
        return Core::failure(LocalizationErrorCode::InvalidData,
                             "localization table entry count exceeds the addressable slot range");
    }
    if (desc.entries.size() > config.entryCapacity) {
        return Core::failure(LocalizationErrorCode::CapacityExceeded,
                             "localization table does not fit the configured entry capacity");
    }
    if (desc.text.size() > config.textByteCapacity) {
        return Core::failure(LocalizationErrorCode::CapacityExceeded,
                             "localization text blob does not fit the configured text capacity");
    }
    if (desc.text.size() > (std::numeric_limits<Core::u32>::max)()) {
        return Core::failure(LocalizationErrorCode::InvalidData,
                             "localization text blob is not addressable by a u32 offset");
    }

    const auto textSize = static_cast<Core::u32>(desc.text.size());
    for (Core::usize index = 0; index < desc.entries.size(); ++index) {
        const LocalizationEntryDesc& entry = desc.entries[index];
        // Strictly ascending, so a duplicate key fails closed instead of making lookup depend on
        // which of the two the search happened to land on.
        if (index != 0 && entry.keyHash <= desc.entries[index - 1U].keyHash) {
            return Core::failure(LocalizationErrorCode::UnsortedTable,
                                 "localization entries must be strictly ascending by key hash");
        }
        if (entry.textOffset == LocalizationCatalogContract::AbsentTextOffset) {
            if (entry.textLength != 0U) {
                return Core::failure(LocalizationErrorCode::InvalidTextRange,
                                     "localization absent entry must declare a zero text length");
            }
            continue;
        }
        if (entry.textOffset > textSize || entry.textLength > textSize - entry.textOffset) {
            return Core::failure(LocalizationErrorCode::InvalidTextRange,
                                 "localization entry text range lies outside the text blob");
        }
        const std::string_view slice(desc.text.data() + entry.textOffset, entry.textLength);
        if (!Core::isStrictUtf8WithoutNul(slice)) {
            return Core::failure(LocalizationErrorCode::InvalidTextEncoding,
                                 "localization entry text is not NUL-free strict UTF-8");
        }
    }

    const auto identity = createUniqueCatalogIdentity();
    if (!identity) {
        return Core::failure(LocalizationErrorCode::IdentityExhausted,
                             "localization catalog identity space is exhausted");
    }

    try {
        std::pmr::vector<Slot> entries{&resource};
        // Reserve the fixed capacity once. Storage never grows afterwards.
        entries.reserve(config.entryCapacity);
        for (const LocalizationEntryDesc& entry : desc.entries) {
            const bool present = entry.textOffset != LocalizationCatalogContract::AbsentTextOffset;
            entries.push_back(Slot{.keyHash = entry.keyHash,
                                   .textOffset = present ? entry.textOffset : 0U,
                                   .textLength = present ? entry.textLength : 0U,
                                   .present = present});
        }

        std::pmr::vector<char> text{&resource};
        text.reserve(config.textByteCapacity);
        text.assign(desc.text.begin(), desc.text.end());

        return LocalizationCatalog(*identity,
                                   desc.locale,
                                   std::move(entries),
                                   std::move(text),
                                   config.entryCapacity,
                                   config.textByteCapacity);
    } catch (const std::bad_alloc&) {
        return Core::failure(LocalizationErrorCode::AllocationFailed,
                             "localization catalog storage allocation failed");
    }
}

LocalizationCatalog::operator bool() const noexcept
{
    return m_identity != 0 && !m_entries.empty() && m_revision != 0;
}

LocalizedTextId LocalizationCatalog::findTextId(std::string_view key) const noexcept
{
    return findTextIdByKeyHash(localizationKeyHash(key));
}

LocalizedTextId LocalizationCatalog::findTextIdByKeyHash(Core::u64 keyHash) const noexcept
{
    if (m_identity == 0) {
        return LocalizedTextId{};
    }
    const auto position = std::lower_bound(m_entries.begin(),
                                           m_entries.end(),
                                           keyHash,
                                           [](const Slot& slot, Core::u64 value) noexcept {
                                               return slot.keyHash < value;
                                           });
    if (position == m_entries.end() || position->keyHash != keyHash) {
        return LocalizedTextId{};
    }
    const auto slot = static_cast<Core::u32>(position - m_entries.begin());
    // A slot with no translation still yields a valid id. Whether this locale carries text is a
    // resolve()-time answer, so a caller can tell "no such key" from "key exists, not translated".
    return LocalizedTextId(m_identity, slot);
}

const LocalizationCatalog::Slot* LocalizationCatalog::slotFor(LocalizedTextId id) const noexcept
{
    if (m_identity == 0 || !id.hasValue() || id.catalogIdentity() != m_identity) {
        return nullptr;
    }
    if (id.slot() >= m_entries.size()) {
        return nullptr;
    }
    return &m_entries[id.slot()];
}

Core::Result<std::string_view> LocalizationCatalog::resolve(LocalizedTextId id) const noexcept
{
    const Slot* slot = slotFor(id);
    if (slot == nullptr) {
        return Core::failure(LocalizationErrorCode::InvalidTextId,
                             "localized text id is invalid or belongs to another catalog");
    }
    if (!slot->present) {
        return Core::failure(LocalizationErrorCode::MissingText,
                             "localized text is absent for this locale");
    }
    // Validated at Create(), so this cannot run off the blob.
    return std::string_view(m_text.data() + slot->textOffset, slot->textLength);
}

std::string_view LocalizationCatalog::resolveOr(LocalizedTextId id,
                                                std::string_view fallback) const noexcept
{
    const Core::Result<std::string_view> text = resolve(id);
    return text ? *text : fallback;
}

bool LocalizationCatalog::contains(LocalizedTextId id) const noexcept
{
    return slotFor(id) != nullptr;
}

Core::Result<Core::u64> LocalizationCatalog::keyHashOf(LocalizedTextId id) const noexcept
{
    const Slot* slot = slotFor(id);
    if (slot == nullptr) {
        return Core::failure(LocalizationErrorCode::InvalidTextId,
                             "localized text id is invalid or belongs to another catalog");
    }
    return slot->keyHash;
}

} // namespace Tina::Localization
