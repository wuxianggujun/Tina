#include <gtest/gtest.h>

#include "detail/UIStyleSheetStorage.hpp"

#include <tina/ui/UIErrors.hpp>

#include <array>
#include <memory_resource>

namespace Tina::Tests {
namespace {

class CountingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return allocationCount_;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++allocationCount_;
        return upstream_.allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        upstream_.deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::pmr::unsynchronized_pool_resource upstream_;
    usize allocationCount_ = 0;
};

[[nodiscard]] UI::Detail::UIStyleSheetStorage makeStorage(
    std::pmr::memory_resource& resource,
    UI::Detail::UIStyleSheetStorageCapacity capacity = {
        .classCapacity = 4,
        .tokenCapacity = 4,
        .ruleCapacity = 8,
        .bucketCapacity = 8,
        .maxRulesPerBucket = 4,
    })
{
    return UI::Detail::UIStyleSheetStorage(capacity, resource);
}

TEST(UIStyleSheetStorageTests, RegistersStrongClassIdsAndReportsCapacity)
{
    std::pmr::monotonic_buffer_resource resource;
    auto storage = makeStorage(resource, {
        .classCapacity = 2,
        .ruleCapacity = 1,
        .bucketCapacity = 1,
        .maxRulesPerBucket = 1,
    });

    const auto first = storage.registerClass();
    const auto second = storage.registerClass();
    const auto exhausted = storage.registerClass();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->value, 1U);
    EXPECT_EQ(second->value, 2U);
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);
    const auto stats = storage.statistics();
    EXPECT_EQ(stats.registeredClassCount, 2U);
    EXPECT_EQ(stats.classHighWater, 2U);
    EXPECT_EQ(stats.capacityFailureCount, 1U);
}

TEST(UIStyleSheetStorageTests, RegistersColorTokensAndResolvesTokenBackedRule)
{
    std::pmr::monotonic_buffer_resource resource;
    auto storage = makeStorage(resource, {
        .classCapacity = 1,
        .tokenCapacity = 2,
        .ruleCapacity = 2,
        .bucketCapacity = 1,
        .maxRulesPerBucket = 2,
    });
    const auto first = storage.registerColorToken(UI::rgb(0x123456));
    const auto second = storage.registerColorToken(UI::rgb(0xABCDEF));
    const auto exhausted = storage.registerColorToken(UI::rgb(0xFFFFFF));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->value, 1U);
    EXPECT_EQ(second->value, 2U);
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);

    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .colorToken = *first,
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .requiredStates = UI::UIStyleState::Disabled,
            .colorToken = *second,
        },
    };
    ASSERT_TRUE(storage.compile(rules).has_value());

    const auto enabled = storage.resolve(UI::UIStyleRoleId::PanelSurface, {},
                                         UI::UIStyleState::None);
    ASSERT_TRUE(enabled.has_value());
    ASSERT_TRUE(enabled->color.has_value());
    EXPECT_EQ(*enabled->color, UI::rgb(0x123456));
    const auto disabled = storage.resolve(UI::UIStyleRoleId::PanelSurface, {},
                                          UI::UIStyleState::Disabled);
    ASSERT_TRUE(disabled.has_value());
    ASSERT_TRUE(disabled->color.has_value());
    EXPECT_EQ(*disabled->color, UI::rgb(0xABCDEF));

    const auto statistics = storage.statistics();
    EXPECT_EQ(statistics.tokenCapacity, 2U);
    EXPECT_EQ(statistics.registeredTokenCount, 2U);
    EXPECT_EQ(statistics.tokenHighWater, 2U);
    EXPECT_EQ(statistics.capacityFailureCount, 1U);
}

TEST(UIStyleSheetStorageTests, RejectsInvalidOrAmbiguousColorTokenRulesAtomically)
{
    std::pmr::monotonic_buffer_resource resource;
    auto storage = makeStorage(resource);
    const UI::UIStyleTokenId token =
        *storage.registerColorToken(UI::rgb(0x135724));
    const std::array baseline{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .colorToken = token,
        },
    };
    ASSERT_TRUE(storage.compile(baseline).has_value());

    const std::array unregistered{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .colorToken = UI::UIStyleTokenId{.value = token.value + 1U},
        },
    };
    const Core::Status unknownToken = storage.compile(unregistered);
    ASSERT_FALSE(unknownToken.has_value());
    EXPECT_EQ(unknownToken.error().code, UI::UIErrorCode::InvalidStyle);

    const std::array ambiguous{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .color = UI::rgb(0xFFFFFF),
            .colorToken = token,
        },
    };
    const Core::Status ambiguousValue = storage.compile(ambiguous);
    ASSERT_FALSE(ambiguousValue.has_value());
    EXPECT_EQ(ambiguousValue.error().code, UI::UIErrorCode::InvalidStyle);

    const auto resolved = storage.resolve(UI::UIStyleRoleId::PanelSurface, {},
                                          UI::UIStyleState::None);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->color.has_value());
    EXPECT_EQ(*resolved->color, UI::rgb(0x135724));
    EXPECT_EQ(storage.statistics().revision, 1U);
    EXPECT_EQ(storage.statistics().compileFailureCount, 2U);
}

TEST(UIStyleSheetStorageTests, ResolvesRoleClassStateAndGlobalSourceOrder)
{
    std::pmr::monotonic_buffer_resource resource;
    auto storage = makeStorage(resource);
    const UI::UIStyleClassId accent = *storage.registerClass();
    const UI::UIStyleClassId compact = *storage.registerClass();
    const auto gray = UI::rgb(0x303030);
    const auto blue = UI::rgb(0x3366FF);
    const auto green = UI::rgb(0x22AA66);
    const auto red = UI::rgb(0xCC3344);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .color = gray,
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .styleClass = accent,
            .color = blue,
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .styleClass = accent,
            .requiredStates = UI::UIStyleState::Hovered,
            .color = green,
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .styleClass = compact,
            .requiredStates = UI::UIStyleState::Hovered,
            .color = red,
        },
    };
    ASSERT_TRUE(storage.compile(rules).has_value());

    const std::array classes{accent, compact};
    const auto normal = storage.resolve(UI::UIStyleRoleId::ButtonPrimary, classes,
                                        UI::UIStyleState::None);
    ASSERT_TRUE(normal.has_value());
    ASSERT_TRUE(normal->color.has_value());
    EXPECT_EQ(*normal->color, blue);
    EXPECT_EQ(normal->candidateRuleCount, 4U);
    EXPECT_EQ(storage.statistics().bucketCandidateHighWater, 2U);

    const auto hovered = storage.resolve(UI::UIStyleRoleId::ButtonPrimary, classes,
                                         UI::UIStyleState::Hovered);
    ASSERT_TRUE(hovered.has_value());
    ASSERT_TRUE(hovered->color.has_value());
    EXPECT_EQ(*hovered->color, red);
    EXPECT_EQ(storage.statistics().revision, 1U);
}

TEST(UIStyleSheetStorageTests, FailedCompilePreservesPublishedSheetAndRevision)
{
    std::pmr::monotonic_buffer_resource resource;
    auto storage = makeStorage(resource);
    const UI::UIStyleClassId styleClass = *storage.registerClass();
    const auto originalColor = UI::rgb(0x123456);
    const std::array original{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .styleClass = styleClass,
            .color = originalColor,
        },
    };
    ASSERT_TRUE(storage.compile(original).has_value());

    const std::array invalid{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .styleClass = UI::UIStyleClassId{.value = 4},
            .color = UI::rgb(0xFFFFFF),
        },
    };
    const Core::Status rejected = storage.compile(invalid);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidStyle);

    const std::array classes{styleClass};
    const auto resolved = storage.resolve(UI::UIStyleRoleId::PanelSurface, classes,
                                          UI::UIStyleState::None);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->color.has_value());
    EXPECT_EQ(*resolved->color, originalColor);
    const auto stats = storage.statistics();
    EXPECT_EQ(stats.revision, 1U);
    EXPECT_EQ(stats.activeRuleCount, 1U);
    EXPECT_EQ(stats.compileFailureCount, 1U);
}

TEST(UIStyleSheetStorageTests, RejectsInvalidStateAndClassSets)
{
    std::pmr::monotonic_buffer_resource resource;
    auto storage = makeStorage(resource);
    const UI::UIStyleClassId styleClass = *storage.registerClass();
    const std::array invalidStateRule{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .styleClass = styleClass,
            .requiredStates = static_cast<UI::UIStyleState>(1U << 12U),
            .color = UI::rgb(0x123456),
        },
    };

    const Core::Status invalidCompile = storage.compile(invalidStateRule);
    ASSERT_FALSE(invalidCompile.has_value());
    EXPECT_EQ(invalidCompile.error().code, UI::UIErrorCode::InvalidStyle);

    const std::array duplicateClasses{styleClass, styleClass};
    const auto duplicateResolve = storage.resolve(
        UI::UIStyleRoleId::ButtonPrimary, duplicateClasses, UI::UIStyleState::None);
    ASSERT_FALSE(duplicateResolve.has_value());
    EXPECT_EQ(duplicateResolve.error().code, UI::UIErrorCode::InvalidStyle);

    const std::array zeroClass{UI::UIStyleClassId{}};
    const auto zeroResolve = storage.resolve(
        UI::UIStyleRoleId::ButtonPrimary, zeroClass, UI::UIStyleState::None);
    ASSERT_FALSE(zeroResolve.has_value());
    EXPECT_EQ(zeroResolve.error().code, UI::UIErrorCode::InvalidStyle);

    const std::array tooManyClasses{
        styleClass, styleClass, styleClass, styleClass, styleClass,
    };
    const auto oversizedResolve = storage.resolve(
        UI::UIStyleRoleId::ButtonPrimary, tooManyClasses, UI::UIStyleState::None);
    ASSERT_FALSE(oversizedResolve.has_value());
    EXPECT_EQ(oversizedResolve.error().code, UI::UIErrorCode::InvalidStyle);

    const auto invalidStateResolve = storage.resolve(
        UI::UIStyleRoleId::ButtonPrimary, std::span<const UI::UIStyleClassId>{},
        static_cast<UI::UIStyleState>(1U << 12U));
    ASSERT_FALSE(invalidStateResolve.has_value());
    EXPECT_EQ(invalidStateResolve.error().code, UI::UIErrorCode::InvalidStyle);
}

TEST(UIStyleSheetStorageTests, RuleCapacityFailurePreservesPublishedSheet)
{
    std::pmr::monotonic_buffer_resource resource;
    auto storage = makeStorage(resource, {
        .classCapacity = 1,
        .ruleCapacity = 1,
        .bucketCapacity = 2,
        .maxRulesPerBucket = 1,
    });
    const auto originalColor = UI::rgb(0x123456);
    const std::array baseline{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .color = originalColor,
        },
    };
    ASSERT_TRUE(storage.compile(baseline).has_value());

    const std::array overflow{
        baseline[0],
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelElevated,
            .color = UI::rgb(0x654321),
        },
    };
    const Core::Status rejected = storage.compile(overflow);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);

    const auto resolved = storage.resolve(UI::UIStyleRoleId::PanelSurface,
                                          std::span<const UI::UIStyleClassId>{},
                                          UI::UIStyleState::None);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->color.has_value());
    EXPECT_EQ(*resolved->color, originalColor);
    EXPECT_EQ(storage.statistics().revision, 1U);
}

TEST(UIStyleSheetStorageTests, EmptyCompileAtomicallyClearsPublishedSheet)
{
    std::pmr::monotonic_buffer_resource resource;
    auto storage = makeStorage(resource);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .color = UI::rgb(0x123456),
        },
    };
    ASSERT_TRUE(storage.compile(rules).has_value());
    ASSERT_TRUE(storage.compile({}).has_value());

    const std::array invalid{
        UI::UIStyleBoxFillRule{
            .role = static_cast<UI::UIStyleRoleId>(0xFF),
            .color = UI::rgb(0x654321),
        },
    };
    const Core::Status rejected = storage.compile(invalid);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidStyle);

    const auto resolved = storage.resolve(UI::UIStyleRoleId::PanelSurface,
                                          std::span<const UI::UIStyleClassId>{},
                                          UI::UIStyleState::None);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_FALSE(resolved->color.has_value());
    const auto stats = storage.statistics();
    EXPECT_EQ(stats.activeRuleCount, 0U);
    EXPECT_EQ(stats.activeBucketCount, 0U);
    EXPECT_EQ(stats.revision, 2U);
    EXPECT_EQ(stats.compileFailureCount, 1U);
}

TEST(UIStyleSheetStorageTests, CapacityFailuresAreAtomicAcrossRuleBucketAndCandidateLimits)
{
    std::pmr::monotonic_buffer_resource resource;
    auto storage = makeStorage(resource, {
        .classCapacity = 2,
        .ruleCapacity = 2,
        .bucketCapacity = 1,
        .maxRulesPerBucket = 1,
    });
    const UI::UIStyleClassId firstClass = *storage.registerClass();
    const UI::UIStyleClassId secondClass = *storage.registerClass();
    const std::array baseline{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .styleClass = firstClass,
            .color = UI::rgb(0x111111),
        },
    };
    ASSERT_TRUE(storage.compile(baseline).has_value());

    const std::array bucketOverflow{
        baseline[0],
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .styleClass = secondClass,
            .color = UI::rgb(0x222222),
        },
    };
    const Core::Status rejectedBucket = storage.compile(bucketOverflow);
    ASSERT_FALSE(rejectedBucket.has_value());
    EXPECT_EQ(rejectedBucket.error().code, UI::UIErrorCode::CapacityExceeded);

    const std::array candidateOverflow{
        baseline[0],
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .styleClass = firstClass,
            .requiredStates = UI::UIStyleState::Focused,
            .color = UI::rgb(0x333333),
        },
    };
    const Core::Status rejectedCandidates = storage.compile(candidateOverflow);
    ASSERT_FALSE(rejectedCandidates.has_value());
    EXPECT_EQ(rejectedCandidates.error().code, UI::UIErrorCode::CapacityExceeded);

    const auto stats = storage.statistics();
    EXPECT_EQ(stats.revision, 1U);
    EXPECT_EQ(stats.activeRuleCount, 1U);
    EXPECT_EQ(stats.capacityFailureCount, 2U);
}

TEST(UIStyleSheetStorageTests, CompileAndResolveStayWithinConstructionPmrBudget)
{
    CountingMemoryResource resource;
    auto storage = makeStorage(resource);
    const UI::UIStyleClassId styleClass = *storage.registerClass();
    const usize constructionAllocationCount = resource.allocationCount();
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonDanger,
            .styleClass = styleClass,
            .color = UI::rgb(0xAA0000),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonDanger,
            .styleClass = styleClass,
            .requiredStates = UI::UIStyleState::Pressed,
            .color = UI::rgb(0x770000),
        },
    };
    const std::array classes{styleClass};

    for (usize iteration = 0; iteration < 300; ++iteration)
    {
        ASSERT_TRUE(storage.compile(rules).has_value());
        const auto resolved = storage.resolve(UI::UIStyleRoleId::ButtonDanger, classes,
                                              UI::UIStyleState::Pressed);
        ASSERT_TRUE(resolved.has_value());
        ASSERT_TRUE(resolved->color.has_value());
        EXPECT_EQ(*resolved->color, UI::rgb(0x770000));
    }
    EXPECT_EQ(resource.allocationCount(), constructionAllocationCount);
}

} // namespace
} // namespace Tina::Tests
