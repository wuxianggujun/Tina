#include <tina/asset/CatalogSnapshot.hpp>

#include <algorithm>
#include <exception>
#include <new>
#include <string>
#include <utility>

namespace Tina::Asset {
namespace {

using Core::u32;
using Core::u8;

enum class VisitColor : u8 {
    White = 0,
    Gray = 1,
    Black = 2,
};

struct StackFrame final {
    u32 entryIndex = 0;
    u32 nextDependency = 0;
};

struct AllocationBlock final {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
    void (*destroy)(void*, std::size_t) = nullptr;
    std::size_t count = 0;
};

class AllocationScope final {
  public:
    explicit AllocationScope(std::pmr::memory_resource& resource) noexcept : m_resource(&resource) {}

    AllocationScope(const AllocationScope&) = delete;
    AllocationScope& operator=(const AllocationScope&) = delete;

    ~AllocationScope()
    {
        releaseAll();
    }

    template <typename T> [[nodiscard]] T* allocateArray(std::size_t count)
    {
        if (count == 0U)
        {
            return nullptr;
        }
        void* pointer = m_resource->allocate(sizeof(T) * count, alignof(T));
        auto* typed = static_cast<T*>(pointer);
        std::uninitialized_default_construct_n(typed, count);
        m_blocks[m_count++] = AllocationBlock{
            .pointer = pointer,
            .bytes = sizeof(T) * count,
            .alignment = alignof(T),
            .destroy =
                [](void* memory, std::size_t elementCount) {
                    std::destroy_n(static_cast<T*>(memory), elementCount);
                },
            .count = count,
        };
        return typed;
    }

    template <typename T> [[nodiscard]] T* allocateRawArray(std::size_t count)
    {
        if (count == 0U)
        {
            return nullptr;
        }
        void* pointer = m_resource->allocate(sizeof(T) * count, alignof(T));
        m_blocks[m_count++] = AllocationBlock{
            .pointer = pointer,
            .bytes = sizeof(T) * count,
            .alignment = alignof(T),
            .destroy = nullptr,
            .count = count,
        };
        return static_cast<T*>(pointer);
    }

    void releaseTracked(void* pointer) noexcept
    {
        for (std::size_t index = 0; index < m_count; ++index)
        {
            if (m_blocks[index].pointer != pointer)
            {
                continue;
            }
            const auto block = m_blocks[index];
            for (std::size_t shift = index + 1U; shift < m_count; ++shift)
            {
                m_blocks[shift - 1U] = m_blocks[shift];
            }
            --m_count;
            if (block.destroy != nullptr)
            {
                block.destroy(block.pointer, block.count);
            }
            m_resource->deallocate(block.pointer, block.bytes, block.alignment);
            return;
        }
    }

    void releaseOwnership(void* pointer) noexcept
    {
        for (std::size_t index = 0; index < m_count; ++index)
        {
            if (m_blocks[index].pointer != pointer)
            {
                continue;
            }
            for (std::size_t shift = index + 1U; shift < m_count; ++shift)
            {
                m_blocks[shift - 1U] = m_blocks[shift];
            }
            --m_count;
            return;
        }
    }

    void releaseAll() noexcept
    {
        while (m_count > 0U)
        {
            --m_count;
            const auto& block = m_blocks[m_count];
            if (block.destroy != nullptr)
            {
                block.destroy(block.pointer, block.count);
            }
            m_resource->deallocate(block.pointer, block.bytes, block.alignment);
        }
    }

  private:
    static constexpr std::size_t MaxBlocks = 8;

    std::pmr::memory_resource* m_resource = nullptr;
    AllocationBlock m_blocks[MaxBlocks]{};
    std::size_t m_count = 0;
};

[[nodiscard]] bool validCatalogConfig(const CatalogConfig& config) noexcept
{
    return config.memoryResource != nullptr && config.maxEntries <= AssetFormat::Wire::MaxManifestEntries &&
           config.maxDependencies <= AssetFormat::Wire::MaxManifestDependencies &&
           config.maxDependenciesPerAsset <= AssetFormat::Wire::MaxDependenciesPerAsset;
}

} // namespace

CatalogSnapshot::CatalogSnapshot(std::pmr::memory_resource* resource, StoredEntry* entries, u32 entryCount,
                                 StoredDependency* dependencies, u32 dependencyCount) noexcept
    : m_resource(resource), m_entries(entries), m_dependencies(dependencies), m_entryCount(entryCount),
      m_dependencyCount(dependencyCount)
{
}

void CatalogSnapshot::reset() noexcept
{
    if (m_resource == nullptr)
    {
        m_entries = nullptr;
        m_dependencies = nullptr;
        m_entryCount = 0;
        m_dependencyCount = 0;
        return;
    }

    if (m_dependencies != nullptr && m_dependencyCount > 0U)
    {
        std::destroy_n(m_dependencies, m_dependencyCount);
        m_resource->deallocate(m_dependencies, sizeof(StoredDependency) * m_dependencyCount,
                               alignof(StoredDependency));
    }
    if (m_entries != nullptr && m_entryCount > 0U)
    {
        std::destroy_n(m_entries, m_entryCount);
        m_resource->deallocate(m_entries, sizeof(StoredEntry) * m_entryCount, alignof(StoredEntry));
    }

    m_resource = nullptr;
    m_entries = nullptr;
    m_dependencies = nullptr;
    m_entryCount = 0;
    m_dependencyCount = 0;
}

CatalogSnapshot::~CatalogSnapshot() noexcept
{
    reset();
}

CatalogSnapshot::CatalogSnapshot(CatalogSnapshot&& other) noexcept
    : m_resource(other.m_resource), m_entries(other.m_entries), m_dependencies(other.m_dependencies),
      m_entryCount(other.m_entryCount), m_dependencyCount(other.m_dependencyCount)
{
    other.m_resource = nullptr;
    other.m_entries = nullptr;
    other.m_dependencies = nullptr;
    other.m_entryCount = 0;
    other.m_dependencyCount = 0;
}

CatalogSnapshot& CatalogSnapshot::operator=(CatalogSnapshot&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    reset();
    m_resource = other.m_resource;
    m_entries = other.m_entries;
    m_dependencies = other.m_dependencies;
    m_entryCount = other.m_entryCount;
    m_dependencyCount = other.m_dependencyCount;
    other.m_resource = nullptr;
    other.m_entries = nullptr;
    other.m_dependencies = nullptr;
    other.m_entryCount = 0;
    other.m_dependencyCount = 0;
    return *this;
}

Core::Result<CatalogSnapshot> CatalogSnapshot::Create(const AssetFormat::CookedManifestView& manifest,
                                                      CatalogConfig config)
{
    if (!validCatalogConfig(config))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid catalog config");
    }
    if (!manifest)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog requires a valid cooked manifest view");
    }

    const auto& header = manifest.header();
    if (header.entryCount > config.maxEntries || header.dependencyCount > config.maxDependencies)
    {
        return Core::failure(AssetErrorCode::CatalogCapacityExceeded, "catalog capacity exceeded");
    }

    for (u32 entryIndex = 0; entryIndex < header.entryCount; ++entryIndex)
    {
        const auto manifestEntry = manifest.entry(entryIndex);
        if (!manifestEntry)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "manifest entry missing during catalog create");
        }
        if (manifestEntry->dependencyCount > config.maxDependenciesPerAsset)
        {
            return Core::failure(AssetErrorCode::CatalogCapacityExceeded, "per-asset dependency capacity exceeded");
        }
    }

    auto& resource = *config.memoryResource;
    AllocationScope scope(resource);

    StoredEntry* entries = nullptr;
    StoredDependency* dependencies = nullptr;

    try
    {
        entries = scope.allocateArray<StoredEntry>(header.entryCount);
        dependencies = scope.allocateArray<StoredDependency>(header.dependencyCount);
        auto* colors = scope.allocateRawArray<VisitColor>(header.entryCount);
        auto* stack = scope.allocateRawArray<StackFrame>(header.entryCount);
        auto* parents = scope.allocateRawArray<u32>(header.entryCount);
        auto* pathScratch = scope.allocateRawArray<u32>(header.entryCount);

        if (header.entryCount > 0U)
        {
            std::fill_n(colors, header.entryCount, VisitColor::White);
            std::fill_n(parents, header.entryCount, 0U);
        }

        for (u32 entryIndex = 0; entryIndex < header.entryCount; ++entryIndex)
        {
            const auto manifestEntry = *manifest.entry(entryIndex);
            entries[entryIndex] = StoredEntry{
                .assetId = manifestEntry.assetId,
                .contentHash = manifestEntry.contentHash,
                .assetKind = manifestEntry.assetKind,
                .assetTypeVersion = manifestEntry.assetTypeVersion,
                .dependencyFirst = manifestEntry.dependencyFirst,
                .dependencyCount = manifestEntry.dependencyCount,
                .cookedFileBytes = manifestEntry.cookedFileBytes,
            };
        }

        for (u32 dependencyIndex = 0; dependencyIndex < header.dependencyCount; ++dependencyIndex)
        {
            const auto manifestDependency = manifest.dependency(dependencyIndex);
            if (!manifestDependency)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "manifest dependency missing during catalog create");
            }

            // Binary search on stored entry AssetId field (first member, sorted).
            u32 begin = 0;
            u32 end = header.entryCount;
            while (begin < end)
            {
                const auto middle = begin + (end - begin) / 2U;
                if (entries[middle].assetId < manifestDependency->assetId)
                {
                    begin = middle + 1U;
                } else
                {
                    end = middle;
                }
            }
            if (begin >= header.entryCount || entries[begin].assetId != manifestDependency->assetId)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "catalog dependency target missing after copy");
            }
            if (entries[begin].assetKind != manifestDependency->expectedKind)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "catalog dependency kind mismatch after copy");
            }

            dependencies[dependencyIndex] = StoredDependency{
                .assetId = manifestDependency->assetId,
                .targetEntryIndex = begin,
                .expectedKind = manifestDependency->expectedKind,
                .flags = manifestDependency->flags,
            };
        }

        for (u32 root = 0; root < header.entryCount; ++root)
        {
            if (colors[root] != VisitColor::White)
            {
                continue;
            }

            u32 stackSize = 0;
            colors[root] = VisitColor::Gray;
            parents[root] = root;
            stack[stackSize++] = StackFrame{.entryIndex = root, .nextDependency = 0};

            while (stackSize > 0U)
            {
                auto& frame = stack[stackSize - 1U];
                const auto& entry = entries[frame.entryIndex];

                if (frame.nextDependency < entry.dependencyCount)
                {
                    const auto dependencyIndex = entry.dependencyFirst + frame.nextDependency;
                    ++frame.nextDependency;
                    const auto target = dependencies[dependencyIndex].targetEntryIndex;

                    if (target == frame.entryIndex)
                    {
                        return Core::failure(AssetErrorCode::DependencyCycle, "catalog dependency self-cycle");
                    }

                    if (colors[target] == VisitColor::Gray)
                    {
                        u32 pathLength = 0;
                        pathScratch[pathLength++] = target;
                        u32 current = frame.entryIndex;
                        while (current != target && pathLength < header.entryCount)
                        {
                            pathScratch[pathLength++] = current;
                            if (parents[current] == current && current != target)
                            {
                                break;
                            }
                            current = parents[current];
                        }
                        if (current == target && pathLength < header.entryCount)
                        {
                            pathScratch[pathLength++] = target;
                        }

                        std::string message = "catalog dependency cycle:";
                        for (u32 index = pathLength; index > 0U; --index)
                        {
                            const auto text = entries[pathScratch[index - 1U]].assetId.canonicalText();
                            message.push_back(' ');
                            message.append(text.data(), text.size());
                        }
                        return Core::failure(AssetErrorCode::DependencyCycle, message);
                    }

                    if (colors[target] == VisitColor::White)
                    {
                        colors[target] = VisitColor::Gray;
                        parents[target] = frame.entryIndex;
                        stack[stackSize++] = StackFrame{.entryIndex = target, .nextDependency = 0};
                    }
                    continue;
                }

                colors[frame.entryIndex] = VisitColor::Black;
                --stackSize;
            }
        }

        scope.releaseTracked(pathScratch);
        scope.releaseTracked(parents);
        scope.releaseTracked(stack);
        scope.releaseTracked(colors);
        scope.releaseOwnership(entries);
        scope.releaseOwnership(dependencies);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog snapshot allocation failed");
    } catch (const std::exception& exception)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, exception.what());
    } catch (...)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog snapshot construction failed");
    }

    return CatalogSnapshot(&resource, entries, header.entryCount, dependencies, header.dependencyCount);
}

std::optional<Core::u32> CatalogSnapshot::find(Core::AssetId assetId) const noexcept
{
    if (m_resource == nullptr || m_entries == nullptr || m_entryCount == 0U || !assetId)
    {
        return std::nullopt;
    }

    u32 begin = 0;
    u32 end = m_entryCount;
    while (begin < end)
    {
        const auto middle = begin + (end - begin) / 2U;
        if (m_entries[middle].assetId < assetId)
        {
            begin = middle + 1U;
        } else
        {
            end = middle;
        }
    }
    if (begin >= m_entryCount || m_entries[begin].assetId != assetId)
    {
        return std::nullopt;
    }
    return begin;
}

std::optional<CatalogEntry> CatalogSnapshot::entry(u32 index) const noexcept
{
    if (m_resource == nullptr || index >= m_entryCount || m_entries == nullptr)
    {
        return std::nullopt;
    }
    const auto& stored = m_entries[index];
    return CatalogEntry{
        .assetId = stored.assetId,
        .contentHash = stored.contentHash,
        .assetKind = stored.assetKind,
        .assetTypeVersion = stored.assetTypeVersion,
        .dependencyCount = stored.dependencyCount,
        .cookedFileBytes = stored.cookedFileBytes,
    };
}

std::optional<CatalogDependency> CatalogSnapshot::dependency(u32 entryIndex, u32 dependencyIndex) const noexcept
{
    if (m_resource == nullptr || entryIndex >= m_entryCount || m_entries == nullptr || m_dependencies == nullptr)
    {
        return std::nullopt;
    }
    const auto& storedEntry = m_entries[entryIndex];
    if (dependencyIndex >= storedEntry.dependencyCount)
    {
        return std::nullopt;
    }
    const auto& stored = m_dependencies[storedEntry.dependencyFirst + dependencyIndex];
    return CatalogDependency{
        .assetId = stored.assetId,
        .targetEntryIndex = stored.targetEntryIndex,
        .expectedKind = stored.expectedKind,
        .flags = stored.flags,
    };
}

} // namespace Tina::Asset
