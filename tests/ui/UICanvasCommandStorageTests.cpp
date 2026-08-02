#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>

#include "detail/UICanvasCommandStorage.hpp"

#include <array>
#include <limits>

namespace Tina::Tests {
namespace {

class ObservingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return allocationCount_;
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++allocationCount_;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize allocationCount_ = 0;
};

[[nodiscard]] Core::AssetId canvasImageAsset()
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x31};
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] UI::UIImageSource canvasImageSource()
{
    return UI::UIImageSource{
        .texture = canvasImageAsset(),
        .sourcePixels = {.x = 4, .y = 6, .width = 20, .height = 18},
        .texturePixelExtent = {.width = 64, .height = 64},
        .intrinsicLogicalSize = {.width = 20.0F, .height = 18.0F},
    };
}

[[nodiscard]] UI::UICanvasCommand canvasImageCommand(UI::UICanvasCommandKind kind)
{
    return UI::UICanvasCommand{
        .kind = kind,
        .bounds = {.x = 1.0F, .y = 2.0F, .width = 30.0F, .height = 24.0F},
        .color = UI::rgba8(255, 200, 100, 192),
        .imageSource = canvasImageSource(),
        .imageSourceInsets = kind == UI::UICanvasCommandKind::NineSlice
                                 ? UI::UIImagePixelInsets{.left = 3, .top = 4, .right = 5, .bottom = 6}
                                 : UI::UIImagePixelInsets{},
        .imageDestinationInsets = kind == UI::UICanvasCommandKind::NineSlice
                                      ? UI::UIEdgeSpacing{.left = 5.0F, .top = 6.0F, .right = 7.0F, .bottom = 8.0F}
                                      : UI::UIEdgeSpacing{},
        .imageSampling = UI::UIImageSampling::Nearest,
    };
}

TEST(UICanvasCommandStorageTests, CopiesAndVisitsCommandsInAssignmentOrder)
{
    UI::Detail::UICanvasCommandStorage storage(2, 3, *std::pmr::get_default_resource());
    std::array<UI::UICanvasCommand, 2> commands{
        UI::UICanvasCommand{
            .bounds = {.x = 1.0F, .y = 2.0F, .width = 3.0F, .height = 4.0F},
            .color = UI::rgb(0x112233),
        },
        UI::UICanvasCommand{
            .bounds = {.x = 5.0F, .y = 6.0F, .width = 7.0F, .height = 8.0F},
            .color = UI::rgb(0x445566),
        },
    };

    const Core::Status assigned = storage.assign(0, commands);
    ASSERT_TRUE(assigned.has_value()) << assigned.error().message;
    commands = {};

    std::array<UI::UICanvasCommand, 2> visited{};
    usize visitedCount = 0;
    storage.forEach(0, [&](const UI::UICanvasCommand& command) noexcept { visited[visitedCount++] = command; });
    ASSERT_EQ(visitedCount, 2U);
    EXPECT_FLOAT_EQ(visited[0].bounds.x, 1.0F);
    EXPECT_EQ(visited[0].color, UI::rgb(0x112233));
    EXPECT_FLOAT_EQ(visited[1].bounds.x, 5.0F);
    EXPECT_EQ(visited[1].color, UI::rgb(0x445566));
    EXPECT_EQ(storage.capacity(), 3U);
    EXPECT_EQ(storage.activeCount(), 2U);
    EXPECT_EQ(storage.highWater(), 2U);
}

TEST(UICanvasCommandStorageTests, CopiesImageAndNineSliceMetadataWithoutBorrowingAuthoringMemory)
{
    UI::Detail::UICanvasCommandStorage storage(1, 2, *std::pmr::get_default_resource());
    std::array commands{
        canvasImageCommand(UI::UICanvasCommandKind::Image),
        canvasImageCommand(UI::UICanvasCommandKind::NineSlice),
    };
    const std::array expected = commands;

    ASSERT_TRUE(storage.assign(0, commands).has_value());
    commands = {};

    std::array<UI::UICanvasCommand, 2> visited{};
    usize visitedCount = 0;
    storage.forEach(0, [&](const UI::UICanvasCommand& command) noexcept {
        visited[visitedCount++] = command;
    });
    ASSERT_EQ(visitedCount, expected.size());
    EXPECT_EQ(visited, expected);
}

TEST(UICanvasCommandStorageTests, RejectsMalformedImageAndNineSliceMetadataAtomically)
{
    UI::Detail::UICanvasCommandStorage storage(1, 1, *std::pmr::get_default_resource());
    std::array malformed{
        canvasImageCommand(UI::UICanvasCommandKind::Image),
        canvasImageCommand(UI::UICanvasCommandKind::Image),
        canvasImageCommand(UI::UICanvasCommandKind::NineSlice),
        canvasImageCommand(UI::UICanvasCommandKind::NineSlice),
        canvasImageCommand(UI::UICanvasCommandKind::NineSlice),
    };
    malformed[0].imageSource.sourcePixels.width = 128;
    malformed[1].imageSourceInsets.left = 1;
    malformed[2].imageSourceInsets.left = 16;
    malformed[2].imageSourceInsets.right = 8;
    malformed[3].imageDestinationInsets.bottom = (std::numeric_limits<float>::quiet_NaN)();
    malformed[4].cornerRadius = 1.0F;

    for (const UI::UICanvasCommand& command : malformed)
    {
        const Core::Status result = storage.assign(0, std::span(&command, 1));
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, UI::UIErrorCode::InvalidElementDescriptor);
        EXPECT_EQ(storage.activeCount(), 0U);
        EXPECT_EQ(storage.highWater(), 0U);
    }
}

TEST(UICanvasCommandStorageTests, RejectsInvalidIndexCapacityAndDescriptorAtomically)
{
    UI::Detail::UICanvasCommandStorage storage(1, 2, *std::pmr::get_default_resource());
    const std::array validCommands{
        UI::UICanvasCommand{.bounds = {.width = 1.0F, .height = 1.0F}},
        UI::UICanvasCommand{.bounds = {.width = 2.0F, .height = 2.0F}},
    };

    const Core::Status invalidIndex = storage.assign(1, validCommands);
    ASSERT_FALSE(invalidIndex.has_value());
    EXPECT_EQ(invalidIndex.error().code, Core::CoreErrorCode::Internal);

    const std::array tooMany{
        validCommands[0],
        validCommands[1],
        UI::UICanvasCommand{.bounds = {.width = 3.0F, .height = 3.0F}},
    };
    const Core::Status exhausted = storage.assign(0, tooMany);
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);

    const UI::UICanvasCommand invalid{
        .bounds =
            {
                .width = (std::numeric_limits<float>::quiet_NaN)(),
                .height = 1.0F,
            },
    };
    const Core::Status invalidDescriptor = storage.assign(0, std::span(&invalid, 1));
    ASSERT_FALSE(invalidDescriptor.has_value());
    EXPECT_EQ(invalidDescriptor.error().code, UI::UIErrorCode::InvalidElementDescriptor);
    EXPECT_EQ(storage.activeCount(), 0U);
    EXPECT_EQ(storage.highWater(), 0U);
}

TEST(UICanvasCommandStorageTests, ReleaseReturnsSlotsForReuseAndPreservesHighWater)
{
    UI::Detail::UICanvasCommandStorage storage(2, 2, *std::pmr::get_default_resource());
    const std::array commands{
        UI::UICanvasCommand{.bounds = {.width = 1.0F, .height = 1.0F}},
        UI::UICanvasCommand{.bounds = {.width = 2.0F, .height = 2.0F}},
    };
    ASSERT_TRUE(storage.assign(0, commands).has_value());
    storage.release(0);
    EXPECT_EQ(storage.activeCount(), 0U);
    EXPECT_EQ(storage.highWater(), 2U);

    ASSERT_TRUE(storage.assign(1, std::span(commands.data(), 1)).has_value());
    usize visitedCount = 0;
    storage.forEach(1, [&](const UI::UICanvasCommand&) noexcept { ++visitedCount; });
    EXPECT_EQ(visitedCount, 1U);
    EXPECT_EQ(storage.activeCount(), 1U);
    EXPECT_EQ(storage.highWater(), 2U);
}

TEST(UICanvasCommandStorageTests, ReservationExcludesOrdinaryAssignmentsAndPublishesExplicitly)
{
    UI::Detail::UICanvasCommandStorage storage(4, 3, *std::pmr::get_default_resource());
    const std::array commands{
        UI::UICanvasCommand{.bounds = {.width = 1.0F, .height = 1.0F}},
        UI::UICanvasCommand{.bounds = {.width = 2.0F, .height = 2.0F}},
    };

    auto reservation = storage.reserve(2);
    ASSERT_TRUE(reservation.has_value()) << (reservation ? "" : reservation.error().message);
    EXPECT_EQ(reservation->remaining, 2U);
    EXPECT_EQ(storage.outstandingReservedCount(), 2U);
    EXPECT_EQ(storage.reservationRequestedCount(), 2U);
    EXPECT_EQ(storage.reservationReservedCount(), 2U);
    EXPECT_EQ(storage.reservationPublishedCount(), 0U);
    EXPECT_EQ(storage.reservationCapacityFailureCount(), 0U);

    const Core::Status excluded = storage.assign(0, commands);
    ASSERT_FALSE(excluded.has_value());
    EXPECT_EQ(excluded.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(storage.activeCount(), 0U);

    ASSERT_TRUE(storage.assignReserved(1, std::span(commands.data(), 1), *reservation).has_value());
    EXPECT_EQ(reservation->remaining, 1U);
    EXPECT_EQ(storage.outstandingReservedCount(), 1U);
    EXPECT_EQ(storage.reservationPublishedCount(), 1U);
    EXPECT_EQ(storage.activeCount(), 1U);

    ASSERT_TRUE(storage.assign(2, std::span(commands.data(), 1)).has_value());
    EXPECT_EQ(storage.activeCount(), 2U);
    storage.releaseReservation(*reservation);
    storage.releaseReservation(*reservation);
    EXPECT_EQ(reservation->remaining, 0U);
    EXPECT_EQ(storage.outstandingReservedCount(), 0U);
    EXPECT_EQ(storage.activeCount(), 2U);
}

TEST(UICanvasCommandStorageTests, FailedReservationAndReservedAssignmentAreAtomic)
{
    UI::Detail::UICanvasCommandStorage storage(2, 2, *std::pmr::get_default_resource());
    const UI::UICanvasCommand valid{.bounds = {.width = 1.0F, .height = 1.0F}};
    auto reservation = storage.reserve(1);
    ASSERT_TRUE(reservation.has_value());

    auto overflow = storage.reserve(2);
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(storage.reservationRequestedCount(), 3U);
    EXPECT_EQ(storage.reservationReservedCount(), 1U);
    EXPECT_EQ(storage.reservationCapacityFailureCount(), 1U);
    EXPECT_EQ(storage.outstandingReservedCount(), 1U);

    UI::UICanvasCommand invalid = valid;
    invalid.bounds.width = (std::numeric_limits<float>::quiet_NaN)();
    const Core::Status rejected = storage.assignReserved(0, std::span(&invalid, 1), *reservation);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);
    EXPECT_EQ(reservation->remaining, 1U);
    EXPECT_EQ(storage.outstandingReservedCount(), 1U);
    EXPECT_EQ(storage.reservationPublishedCount(), 0U);
    EXPECT_EQ(storage.activeCount(), 0U);

    const std::array tooMany{valid, valid};
    const Core::Status exceedsReservation = storage.assignReserved(0, tooMany, *reservation);
    ASSERT_FALSE(exceedsReservation.has_value());
    EXPECT_EQ(exceedsReservation.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(reservation->remaining, 1U);
    EXPECT_EQ(storage.outstandingReservedCount(), 1U);
    EXPECT_EQ(storage.activeCount(), 0U);

    storage.releaseReservation(*reservation);
    EXPECT_EQ(storage.outstandingReservedCount(), 0U);
}

TEST(UICanvasCommandStorageTests, ReservationOperationsDoNotGrowPmrStorageAfterConstruction)
{
    ObservingMemoryResource resource;
    UI::Detail::UICanvasCommandStorage storage(2, 2, resource);
    const usize allocationsAfterConstruction = resource.allocationCount();
    const std::array commands{
        UI::UICanvasCommand{.bounds = {.width = 1.0F, .height = 1.0F}},
        UI::UICanvasCommand{.bounds = {.width = 2.0F, .height = 2.0F}},
    };

    auto reservation = storage.reserve(commands.size());
    ASSERT_TRUE(reservation.has_value());
    ASSERT_TRUE(storage.assignReserved(0, commands, *reservation).has_value());
    storage.releaseReservation(*reservation);
    storage.release(0);
    EXPECT_EQ(resource.allocationCount(), allocationsAfterConstruction);
}

} // namespace
} // namespace Tina::Tests
