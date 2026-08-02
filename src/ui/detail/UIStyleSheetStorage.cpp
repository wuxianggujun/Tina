#include "UIStyleSheetStorage.hpp"

#include "UIStyleRoleResolver.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] constexpr bool isValidStyleStateMask(UIStyleState states) noexcept
{
    const u16 bits = static_cast<u16>(states);
    return (bits & ~static_cast<u16>(UIStyleState::All)) == 0;
}

[[nodiscard]] Core::Status invalidStyle(std::string_view message)
{
    return Core::failure(UIErrorCode::InvalidStyle, message);
}

} // namespace

UIStyleSheetStorage::Buffer::Buffer(UIStyleSheetStorageCapacity capacity,
                                    std::pmr::memory_resource& resource)
    : rules(&resource), buckets(&resource), candidateRuleIndices(&resource)
{
    rules.reserve(capacity.ruleCapacity);
    buckets.reserve(capacity.bucketCapacity);
    candidateRuleIndices.reserve(capacity.ruleCapacity);
}

void UIStyleSheetStorage::Buffer::clear() noexcept
{
    rules.clear();
    buckets.clear();
    candidateRuleIndices.clear();
}

UIStyleSheetStorage::UIStyleSheetStorage(UIStyleSheetStorageCapacity capacity,
                                         std::pmr::memory_resource& resource)
    : capacity_(capacity), buffers_{Buffer(capacity, resource), Buffer(capacity, resource)}
{
}

Core::Result<UIStyleClassId> UIStyleSheetStorage::registerClass()
{
    if (registeredClassCount_ >= capacity_.classCapacity ||
        registeredClassCount_ >= (std::numeric_limits<u32>::max)())
    {
        ++capacityFailureCount_;
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI style class capacity has been exhausted");
    }

    ++registeredClassCount_;
    classHighWater_ = (std::max)(classHighWater_, registeredClassCount_);
    return UIStyleClassId{.value = static_cast<u32>(registeredClassCount_)};
}

Core::Status UIStyleSheetStorage::validateRule(const UIStyleBoxFillRule& rule) const
{
    if (!isValidStyleRole(rule.role))
    {
        return invalidStyle("UI style rule contains an invalid role");
    }
    if (rule.styleClass.hasValue() && rule.styleClass.value > registeredClassCount_)
    {
        return invalidStyle("UI style rule references an unregistered class");
    }
    if (!isValidStyleStateMask(rule.requiredStates))
    {
        return invalidStyle("UI style rule contains an invalid state mask");
    }
    return Core::success();
}

Core::Status UIStyleSheetStorage::compile(std::span<const UIStyleBoxFillRule> rules)
{
    Buffer& candidate = buffers_[1U - activeBufferIndex_];
    candidate.clear();

    const auto failCompile = [this](Core::Error error, bool capacityFailure) {
        ++compileFailureCount_;
        if (capacityFailure)
        {
            ++capacityFailureCount_;
        }
        return Core::failure(std::move(error));
    };

    if (rules.size() > capacity_.ruleCapacity)
    {
        return failCompile(
            Core::Error{UIErrorCode::CapacityExceeded,
                        "UI style rule capacity has been exhausted"},
            true);
    }

    for (const UIStyleBoxFillRule& rule : rules)
    {
        if (Core::Status valid = validateRule(rule); !valid)
        {
            return failCompile(valid.error(), false);
        }

        const auto bucketIterator = std::find_if(
            candidate.buckets.begin(), candidate.buckets.end(),
            [&rule](const Bucket& bucket) {
                return bucket.role == rule.role && bucket.styleClass == rule.styleClass;
            });
        const bool createBucket = bucketIterator == candidate.buckets.end();
        if (createBucket)
        {
            if (candidate.buckets.size() >= capacity_.bucketCapacity)
            {
                return failCompile(
                    Core::Error{UIErrorCode::CapacityExceeded,
                                "UI style bucket capacity has been exhausted"},
                    true);
            }
            candidate.buckets.push_back(
                Bucket{.role = rule.role, .styleClass = rule.styleClass});
        }

        Bucket& bucket = createBucket ? candidate.buckets.back() : *bucketIterator;
        if (bucket.candidateCount >= capacity_.maxRulesPerBucket)
        {
            return failCompile(
                Core::Error{UIErrorCode::CapacityExceeded,
                            "UI style bucket candidate capacity has been exhausted"},
                true);
        }
        ++bucket.candidateCount;
        candidate.rules.push_back(rule);
    }

    std::sort(candidate.buckets.begin(), candidate.buckets.end(),
              [](const Bucket& left, const Bucket& right) {
                  if (left.role != right.role)
                  {
                      return left.role < right.role;
                  }
                  return left.styleClass < right.styleClass;
              });

    usize candidateOffset = 0;
    for (Bucket& bucket : candidate.buckets)
    {
        bucket.candidateOffset = candidateOffset;
        bucket.writeCount = 0;
        candidateOffset += bucket.candidateCount;
    }
    candidate.candidateRuleIndices.resize(rules.size());

    for (usize ruleIndex = 0; ruleIndex < candidate.rules.size(); ++ruleIndex)
    {
        const UIStyleBoxFillRule& rule = candidate.rules[ruleIndex];
        const Bucket* bucketView = findBucket(candidate, rule.role, rule.styleClass);
        if (bucketView == nullptr)
        {
            return failCompile(
                Core::Error{UIErrorCode::InvalidStyle,
                            "UI style compiler lost a precompiled bucket"},
                false);
        }
        Bucket& bucket = candidate.buckets[
            static_cast<usize>(bucketView - candidate.buckets.data())];
        candidate.candidateRuleIndices[bucket.candidateOffset + bucket.writeCount] = ruleIndex;
        ++bucket.writeCount;
    }
    for (Bucket& bucket : candidate.buckets)
    {
        bucket.writeCount = 0;
    }

    activeBufferIndex_ = 1U - activeBufferIndex_;
    ++revision_;
    ruleHighWater_ = (std::max)(ruleHighWater_, candidate.rules.size());
    bucketHighWater_ = (std::max)(bucketHighWater_, candidate.buckets.size());
    for (const Bucket& bucket : candidate.buckets)
    {
        bucketCandidateHighWater_ =
            (std::max)(bucketCandidateHighWater_, bucket.candidateCount);
    }
    return Core::success();
}

Core::Status UIStyleSheetStorage::validateResolveInput(
    UIStyleRoleId role, std::span<const UIStyleClassId> classes,
    UIStyleState states) const
{
    if (!isValidStyleRole(role))
    {
        return invalidStyle("UI style resolve contains an invalid role");
    }
    if (Core::Status validClasses = validateClasses(classes); !validClasses)
    {
        return validClasses;
    }
    if (!isValidStyleStateMask(states))
    {
        return invalidStyle("UI style resolve contains an invalid state mask");
    }
    return Core::success();
}

Core::Status UIStyleSheetStorage::validateClasses(
    std::span<const UIStyleClassId> classes) const
{
    if (classes.size() > MaxStyleClassesPerNode)
    {
        return invalidStyle("UI style resolve exceeds the per-node class limit");
    }
    for (usize index = 0; index < classes.size(); ++index)
    {
        const UIStyleClassId styleClass = classes[index];
        if (!styleClass.hasValue() || styleClass.value > registeredClassCount_)
        {
            return invalidStyle("UI style resolve references an invalid class");
        }
        for (usize previous = 0; previous < index; ++previous)
        {
            if (classes[previous] == styleClass)
            {
                return invalidStyle("UI style resolve contains a duplicate class");
            }
        }
    }
    return Core::success();
}

const UIStyleSheetStorage::Bucket* UIStyleSheetStorage::findBucket(
    const Buffer& buffer, UIStyleRoleId role, UIStyleClassId styleClass) const noexcept
{
    const auto iterator = std::lower_bound(
        buffer.buckets.begin(), buffer.buckets.end(), std::pair{role, styleClass},
        [](const Bucket& bucket, const auto& key) {
            if (bucket.role != key.first)
            {
                return bucket.role < key.first;
            }
            return bucket.styleClass < key.second;
        });
    if (iterator == buffer.buckets.end() || iterator->role != role ||
        iterator->styleClass != styleClass)
    {
        return nullptr;
    }
    return &*iterator;
}

Core::Result<UIStyleBoxFillResolution> UIStyleSheetStorage::resolve(
    UIStyleRoleId role, std::span<const UIStyleClassId> classes,
    UIStyleState states) const
{
    if (Core::Status valid = validateResolveInput(role, classes, states); !valid)
    {
        return Core::failure(valid.error());
    }

    const Buffer& active = buffers_[activeBufferIndex_];
    UIStyleBoxFillResolution resolution{};
    usize winningRuleIndex = 0;
    bool hasWinningRule = false;

    const auto inspectBucket = [&](const Bucket* bucket) {
        if (bucket == nullptr)
        {
            return;
        }
        resolution.candidateRuleCount += bucket->candidateCount;
        for (usize index = 0; index < bucket->candidateCount; ++index)
        {
            const usize candidateIndex = bucket->candidateOffset + index;
            const usize ruleIndex = active.candidateRuleIndices[candidateIndex];
            const UIStyleBoxFillRule& rule = active.rules[ruleIndex];
            if (!hasStyleState(states, rule.requiredStates))
            {
                continue;
            }
            if (!hasWinningRule || ruleIndex > winningRuleIndex)
            {
                hasWinningRule = true;
                winningRuleIndex = ruleIndex;
                resolution.color = rule.color;
            }
        }
    };

    inspectBucket(findBucket(active, role, {}));
    for (UIStyleClassId styleClass : classes)
    {
        inspectBucket(findBucket(active, role, styleClass));
    }
    return resolution;
}

UIStyleSheetStorageStatistics UIStyleSheetStorage::statistics() const noexcept
{
    const Buffer& active = buffers_[activeBufferIndex_];
    return UIStyleSheetStorageStatistics{
        .classCapacity = capacity_.classCapacity,
        .registeredClassCount = registeredClassCount_,
        .classHighWater = classHighWater_,
        .ruleCapacity = capacity_.ruleCapacity,
        .activeRuleCount = active.rules.size(),
        .ruleHighWater = ruleHighWater_,
        .bucketCapacity = capacity_.bucketCapacity,
        .activeBucketCount = active.buckets.size(),
        .bucketHighWater = bucketHighWater_,
        .maxRulesPerBucket = capacity_.maxRulesPerBucket,
        .bucketCandidateHighWater = bucketCandidateHighWater_,
        .compileFailureCount = compileFailureCount_,
        .capacityFailureCount = capacityFailureCount_,
        .revision = revision_,
    };
}

} // namespace Tina::UI::Detail
