#include <gtest/gtest.h>

#include <tina/save/SaveErrors.hpp>
#include <tina/save/SaveMigration.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace Tina::Tests {
namespace {

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char value : text) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

[[nodiscard]] std::string textOf(std::span<const std::byte> bytes)
{
    std::string result;
    result.reserve(bytes.size());
    for (const std::byte value : bytes) {
        result.push_back(static_cast<char>(value));
    }
    return result;
}

// Appends a marker so a multi-step path shows the order steps actually ran in,
// not merely that the final version was reached.
[[nodiscard]] Save::SaveMigrationFunction appendMarker(std::string marker)
{
    return Save::SaveMigrationFunction{
        [marker = std::move(marker)](std::span<const std::byte> payload)
            -> Core::Result<std::vector<std::byte>> {
            std::string text = textOf(payload);
            text.append(marker);
            return bytesOf(text);
        }};
}

} // namespace

TEST(SaveMigrationTest, CreateRejectsZeroAndOversizedLimits)
{
    EXPECT_FALSE(Save::SaveMigrationPipeline::Create(
                     Save::SaveMigrationConfig{.maxPayloadBytes = 0})
                     .has_value());
    EXPECT_FALSE(Save::SaveMigrationPipeline::Create(Save::SaveMigrationConfig{
                                                         .maxPayloadBytes =
                                                             Save::MaxSavePayloadBytes + 1})
                     .has_value());
    EXPECT_FALSE(Save::SaveMigrationPipeline::Create(
                     Save::SaveMigrationConfig{.stepCapacity = 0})
                     .has_value());
    EXPECT_FALSE(
        Save::SaveMigrationPipeline::Create(
            Save::SaveMigrationConfig{
                .stepCapacity = Save::MaxSaveMigrationStepCapacity + 1})
            .has_value());

    const auto pipeline = Save::SaveMigrationPipeline::Create();
    EXPECT_TRUE(pipeline.has_value());
}

// The graph is downgrade-free by construction: a step must strictly increase the
// version, so no edge can ever walk a save backwards.
TEST(SaveMigrationTest, AddStepRequiresStrictlyIncreasingNonZeroVersions)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());

    EXPECT_FALSE(pipeline->addStep(0, 1, appendMarker("a")).has_value());
    EXPECT_FALSE(pipeline->addStep(1, 0, appendMarker("a")).has_value());
    EXPECT_FALSE(pipeline->addStep(2, 2, appendMarker("a")).has_value());
    EXPECT_FALSE(pipeline->addStep(3, 2, appendMarker("a")).has_value());
    // A null callable would make the edge silently skip its transformation.
    EXPECT_FALSE(pipeline->addStep(1, 2, Save::SaveMigrationFunction{}).has_value());

    EXPECT_TRUE(pipeline->addStep(1, 2, appendMarker("a")).has_value());
}

// One edge per source version is what makes migration deterministic. A second edge
// out of the same version would make the result depend on registration order.
TEST(SaveMigrationTest, AddStepRejectsASecondEdgeFromTheSameVersion)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2, appendMarker("a")).has_value());

    const auto duplicate = pipeline->addStep(1, 3, appendMarker("b"));
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, Save::SaveErrorCode::DuplicateMigrationStep);

    // A different source version is still accepted.
    EXPECT_TRUE(pipeline->addStep(2, 3, appendMarker("b")).has_value());
}

TEST(SaveMigrationTest, AddStepFailsClosedAtCapacity)
{
    auto pipeline =
        Save::SaveMigrationPipeline::Create(Save::SaveMigrationConfig{.stepCapacity = 2});
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2, appendMarker("a")).has_value());
    ASSERT_TRUE(pipeline->addStep(2, 3, appendMarker("b")).has_value());

    const auto overflow = pipeline->addStep(3, 4, appendMarker("c"));
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, Core::CoreErrorCode::CapacityExceeded);
}

// Same version in and out: the payload must come back untouched, and no step may
// run. This is the common case for an up-to-date save.
TEST(SaveMigrationTest, MigrateToTheSameVersionIsAnIdentityAndRunsNoStep)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2, appendMarker("|step1to2")).has_value());

    const std::vector<std::byte> payload = bytesOf("state");
    const auto migrated = pipeline->migrate(1, payload, 1);
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(textOf(*migrated), "state");
}

TEST(SaveMigrationTest, MigrateAppliesStepsInAscendingOrder)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    // Registered out of order on purpose; traversal must still follow the graph.
    ASSERT_TRUE(pipeline->addStep(3, 4, appendMarker("|c")).has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2, appendMarker("|a")).has_value());
    ASSERT_TRUE(pipeline->addStep(2, 3, appendMarker("|b")).has_value());

    const auto migrated = pipeline->migrate(1, bytesOf("v1"), 4);
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(textOf(*migrated), "v1|a|b|c");
}

// A step is allowed to skip versions (1 -> 3), so the walk must match on the
// source version rather than assume single increments.
TEST(SaveMigrationTest, MigrateFollowsVersionSkippingSteps)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 3, appendMarker("|jump")).has_value());
    ASSERT_TRUE(pipeline->addStep(3, 4, appendMarker("|tail")).has_value());

    const auto migrated = pipeline->migrate(1, bytesOf("v1"), 4);
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(textOf(*migrated), "v1|jump|tail");
}

TEST(SaveMigrationTest, MigrateRejectsZeroVersionsAndDowngrades)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2, appendMarker("|a")).has_value());

    for (const auto& [source, target] :
         {std::pair<Core::u32, Core::u32>{0, 2}, std::pair<Core::u32, Core::u32>{1, 0}}) {
        const auto migrated = pipeline->migrate(source, bytesOf("x"), target);
        ASSERT_FALSE(migrated.has_value());
        EXPECT_EQ(migrated.error().code, Save::SaveErrorCode::InvalidMigrationVersion);
    }

    // Downgrades are refused rather than silently returning the input.
    const auto downgrade = pipeline->migrate(2, bytesOf("x"), 1);
    ASSERT_FALSE(downgrade.has_value());
    EXPECT_EQ(downgrade.error().code, Save::SaveErrorCode::InvalidMigrationVersion);
}

TEST(SaveMigrationTest, MigrateReportsAMissingStep)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2, appendMarker("|a")).has_value());
    // No edge out of 2, so the path to 3 is incomplete.

    const auto migrated = pipeline->migrate(1, bytesOf("v1"), 3);
    ASSERT_FALSE(migrated.has_value());
    EXPECT_EQ(migrated.error().code, Save::SaveErrorCode::MigrationPathMissing);

    // An empty graph cannot reach any newer version either.
    auto empty = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(empty.has_value());
    const auto unreachable = empty->migrate(1, bytesOf("v1"), 2);
    ASSERT_FALSE(unreachable.has_value());
    EXPECT_EQ(unreachable.error().code, Save::SaveErrorCode::MigrationPathMissing);
}

// A step that overshoots the target must not run. Applying it would produce a
// payload at a version the caller never asked for and cannot interpret.
TEST(SaveMigrationTest, MigrateRefusesToOvershootTheTargetVersion)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 5, appendMarker("|far")).has_value());

    const auto migrated = pipeline->migrate(1, bytesOf("v1"), 3);
    ASSERT_FALSE(migrated.has_value());
    EXPECT_EQ(migrated.error().code, Save::SaveErrorCode::MigrationPathMissing);
}

TEST(SaveMigrationTest, MigrateRejectsSourcePayloadAboveTheLimit)
{
    auto pipeline = Save::SaveMigrationPipeline::Create(
        Save::SaveMigrationConfig{.maxPayloadBytes = 8});
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2, appendMarker("|a")).has_value());

    const std::vector<std::byte> oversized(9, std::byte{'x'});
    const auto migrated = pipeline->migrate(1, oversized, 2);
    ASSERT_FALSE(migrated.has_value());
    EXPECT_EQ(migrated.error().code, Save::SaveErrorCode::PayloadTooLarge);

    // Exactly at the limit is deliverable, not merely close to it.
    const std::vector<std::byte> atLimit(8, std::byte{'x'});
    EXPECT_TRUE(pipeline->migrate(1, atLimit, 1).has_value());
}

// The limit also applies to what a step produces, so a migration cannot grow a
// payload past the bound one step at a time.
TEST(SaveMigrationTest, MigrateRejectsAStepThatGrowsPastTheLimit)
{
    auto pipeline = Save::SaveMigrationPipeline::Create(
        Save::SaveMigrationConfig{.maxPayloadBytes = 4});
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2,
                                  Save::SaveMigrationFunction{
                                      [](std::span<const std::byte>)
                                          -> Core::Result<std::vector<std::byte>> {
                                          return std::vector<std::byte>(5, std::byte{'y'});
                                      }})
                    .has_value());

    const auto migrated = pipeline->migrate(1, bytesOf("ab"), 2);
    ASSERT_FALSE(migrated.has_value());
    EXPECT_EQ(migrated.error().code, Save::SaveErrorCode::PayloadTooLarge);
}

// A failing step surfaces its own error code, and the context chain names the edge
// so an operator can tell which transformation rejected the data.
TEST(SaveMigrationTest, MigratePropagatesStepFailureWithEdgeContext)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2, appendMarker("|a")).has_value());
    ASSERT_TRUE(pipeline->addStep(2, 3,
                                  Save::SaveMigrationFunction{
                                      [](std::span<const std::byte>)
                                          -> Core::Result<std::vector<std::byte>> {
                                          return Core::failure(
                                              Save::SaveErrorCode::CorruptData,
                                              "step rejected the payload");
                                      }})
                    .has_value());

    const auto migrated = pipeline->migrate(1, bytesOf("v1"), 3);
    ASSERT_FALSE(migrated.has_value());
    EXPECT_EQ(migrated.error().code, Save::SaveErrorCode::CorruptData);
    ASSERT_FALSE(migrated.error().context.empty());
    EXPECT_EQ(migrated.error().context.front().operation, "SaveMigrationPipeline::migrate");
    EXPECT_EQ(migrated.error().context.front().detail, "2 -> 3");
}

// A throwing step is contained and reported as MigrationFailed. Product migration
// code is arbitrary caller code, so an escaping exception would cross a module
// boundary that publishes Result.
TEST(SaveMigrationTest, MigrateContainsAThrowingStep)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2,
                                  Save::SaveMigrationFunction{
                                      [](std::span<const std::byte>)
                                          -> Core::Result<std::vector<std::byte>> {
                                          throw std::runtime_error{"boom"};
                                      }})
                    .has_value());

    const auto migrated = pipeline->migrate(1, bytesOf("v1"), 2);
    ASSERT_FALSE(migrated.has_value());
    EXPECT_EQ(migrated.error().code, Save::SaveErrorCode::MigrationFailed);
}

TEST(SaveMigrationTest, MigrateAcceptsAnEmptyPayload)
{
    auto pipeline = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(pipeline.has_value());
    ASSERT_TRUE(pipeline->addStep(1, 2, appendMarker("seeded")).has_value());

    const auto migrated = pipeline->migrate(1, std::span<const std::byte>{}, 2);
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(textOf(*migrated), "seeded");
}

// Moving the pipeline must carry the registered graph with it; a moved-from
// pipeline is not reused.
TEST(SaveMigrationTest, MoveConstructionPreservesTheRegisteredGraph)
{
    auto original = Save::SaveMigrationPipeline::Create();
    ASSERT_TRUE(original.has_value());
    ASSERT_TRUE(original->addStep(1, 2, appendMarker("|moved")).has_value());

    Save::SaveMigrationPipeline moved{std::move(*original)};
    const auto migrated = moved.migrate(1, bytesOf("v1"), 2);
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(textOf(*migrated), "v1|moved");
}

} // namespace Tina::Tests
