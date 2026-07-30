#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UICommittedLayout.hpp>
#include <tina/ui/UICommittedPaint.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

struct UIPaintSnapshotSourceAdapter final {
    using CountEntries = Core::Result<usize> (*)(const void* context, const UICommittedLayoutEntry& layoutEntry);
    using AppendEntries = void (*)(void* context, std::pmr::vector<UICommittedPaintEntry>& output,
                                   const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal) noexcept;

    CountEntries countEntries = nullptr;
    AppendEntries appendEntries = nullptr;
};

class UIPaintSnapshotBuilder final {
  public:
    explicit UIPaintSnapshotBuilder(usize entryCapacity) noexcept;

    [[nodiscard]] Core::Result<usize> validateCapacity(std::span<const UICommittedLayoutEntry> layoutEntries,
                                                       const void* sourceContext,
                                                       UIPaintSnapshotSourceAdapter sourceAdapter) const;

    [[nodiscard]] Core::Status build(std::pmr::vector<UICommittedPaintEntry>& output,
                                     std::span<const UICommittedLayoutEntry> layoutEntries, void* sourceContext,
                                     UIPaintSnapshotSourceAdapter sourceAdapter) const;

  private:
    usize entryCapacity_ = 0;
};

} // namespace Tina::UI::Detail
