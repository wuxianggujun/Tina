#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIStyle.hpp>

#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

inline constexpr usize MaxStyleClassesPerNode = 4;

struct UIStyleSheetStorageCapacity final {
    usize classCapacity = 0;
    usize ruleCapacity = 0;
    usize bucketCapacity = 0;
    usize maxRulesPerBucket = 0;
};

struct UIStyleBoxFillResolution final {
    std::optional<UIStraightSrgba8Color> color{};
    usize candidateRuleCount = 0;
};

struct UIStyleSheetStorageStatistics final {
    usize classCapacity = 0;
    usize registeredClassCount = 0;
    usize classHighWater = 0;
    usize ruleCapacity = 0;
    usize activeRuleCount = 0;
    usize ruleHighWater = 0;
    usize bucketCapacity = 0;
    usize activeBucketCount = 0;
    usize bucketHighWater = 0;
    usize maxRulesPerBucket = 0;
    usize bucketCandidateHighWater = 0;
    usize compileFailureCount = 0;
    usize capacityFailureCount = 0;
    u64 revision = 0;
};

class UIStyleSheetStorage final {
  public:
    UIStyleSheetStorage(UIStyleSheetStorageCapacity capacity,
                        std::pmr::memory_resource& resource);

    // Class registration is independent from atomic sheet replacement. A
    // failed compile preserves the class registry as well as the active sheet.
    [[nodiscard]] Core::Result<UIStyleClassId> registerClass();
    [[nodiscard]] Core::Status validateClasses(
        std::span<const UIStyleClassId> classes) const;
    [[nodiscard]] Core::Status compile(std::span<const UIStyleBoxFillRule> rules);
    [[nodiscard]] Core::Result<UIStyleBoxFillResolution>
    resolve(UIStyleRoleId role, std::span<const UIStyleClassId> classes,
            UIStyleState states) const;
    // The Context validates roles/classes at stylesheet install and element
    // creation. Paint-cache rebuilds use this non-failing path so invariant
    // violations cannot be silently converted into an empty resolution.
    [[nodiscard]] UIStyleBoxFillResolution
    resolveValidated(UIStyleRoleId role, std::span<const UIStyleClassId> classes,
                     UIStyleState states) const noexcept;

    [[nodiscard]] UIStyleSheetStorageStatistics statistics() const noexcept;

  private:
    struct Bucket final {
        UIStyleRoleId role = UIStyleRoleId::None;
        UIStyleClassId styleClass{};
        usize candidateOffset = 0;
        usize candidateCount = 0;
        usize writeCount = 0;
    };

    struct Buffer final {
        Buffer(UIStyleSheetStorageCapacity capacity, std::pmr::memory_resource& resource);

        void clear() noexcept;

        std::pmr::vector<UIStyleBoxFillRule> rules;
        std::pmr::vector<Bucket> buckets;
        std::pmr::vector<usize> candidateRuleIndices;
    };

    [[nodiscard]] Core::Status validateRule(const UIStyleBoxFillRule& rule) const;
    [[nodiscard]] Core::Status validateResolveInput(
        UIStyleRoleId role, std::span<const UIStyleClassId> classes,
        UIStyleState states) const;
    [[nodiscard]] const Bucket* findBucket(
        const Buffer& buffer, UIStyleRoleId role, UIStyleClassId styleClass) const noexcept;

    UIStyleSheetStorageCapacity capacity_{};
    usize registeredClassCount_ = 0;
    usize classHighWater_ = 0;
    usize ruleHighWater_ = 0;
    usize bucketHighWater_ = 0;
    usize bucketCandidateHighWater_ = 0;
    usize compileFailureCount_ = 0;
    usize capacityFailureCount_ = 0;
    u64 revision_ = 0;
    usize activeBufferIndex_ = 0;
    Buffer buffers_[2];
};

} // namespace Tina::UI::Detail
