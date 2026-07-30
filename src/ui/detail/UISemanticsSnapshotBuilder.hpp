#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UICommittedLayout.hpp>
#include <tina/ui/UISemantics.hpp>

#include <limits>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::UI::Detail {

struct UISemanticsSnapshotSource final {
    static constexpr u32 InvalidNodeIndex = (std::numeric_limits<u32>::max)();

    u32 parentNodeIndex = InvalidNodeIndex;
    UISemanticsMode mode = UISemanticsMode::Automatic;
    UISemanticsEntry entry{};
    std::string_view name{};
    std::string_view description{};
    std::string_view valueText{};
};

struct UISemanticsSnapshotSourceAdapter final {
    using ResolveSource = bool (*)(void* context, UINodeId node, UISemanticsSnapshotSource& output) noexcept;

    void* context = nullptr;
    ResolveSource resolve = nullptr;
};

class UISemanticsSnapshotBuilder final {
  public:
    UISemanticsSnapshotBuilder(usize nodeCapacity, usize entryCapacity, std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Status build(std::pmr::vector<UISemanticsEntry>& output,
                                     std::pmr::vector<char>& textOutput,
                                     std::span<const UICommittedLayoutEntry> layoutEntries,
                                     UISemanticsSnapshotSourceAdapter sourceAdapter);

  private:
    static constexpr u32 InvalidEntryIndex = (std::numeric_limits<u32>::max)();

    struct NodeScratch final {
        u32 nearestPublishedEntryIndex = InvalidEntryIndex;
        u32 mergeEntryIndex = InvalidEntryIndex;
        u32 nameTargetEntryIndex = InvalidEntryIndex;
        bool excluded = false;
    };

    struct EntryScratch final {
        usize nameByteCount = 0;
        usize nameOffset = 0;
        usize nameBytesWritten = 0;
        bool hasNamePart = false;
    };

    std::pmr::vector<NodeScratch> nodeScratchByIndex_;
    std::pmr::vector<EntryScratch> entryScratch_;
};

} // namespace Tina::UI::Detail
