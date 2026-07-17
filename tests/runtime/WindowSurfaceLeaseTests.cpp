#include "WindowSurfaceLeaseAccess.hpp"

#include <tina/core/id/GenerationPool.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <thread>

namespace Tina::Tests {
namespace {

struct TestSurfaceRecord final {};

using TestSurfacePool = Core::GenerationPool<TestSurfaceRecord, Integration::WindowSurfaceRegistryTag>;

[[nodiscard]] TestSurfacePool createSurfacePool()
{
    auto pool = TestSurfacePool::Create(1);
    EXPECT_TRUE(pool.has_value());
    return std::move(*pool);
}

[[nodiscard]] Integration::Detail::NativeWindowBinding fakeBinding() noexcept
{
    return Integration::Detail::NativeWindowBinding{
        .kind = Integration::Detail::NativeWindowBindingKind::Win32,
        .nativeDisplay = 0,
        .nativeWindow = 1,
        .bindingRevision = 1,
    };
}

} // namespace

TEST(WindowSurfaceLeaseTest, MoveKeepsExactlyOneLifetimePinAndPrivateDecoderAccess)
{
    auto pool = createSurfacePool();
    auto surface = pool.tryEmplace();
    ASSERT_TRUE(surface.has_value());

    auto control = std::make_shared<Integration::Detail::NativeWindowSurfaceLeaseControl>();
    control->ownerThread = std::this_thread::get_id();
    auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(control, *surface, fakeBinding());
    ASSERT_TRUE(lease.has_value());
    EXPECT_EQ(control->activeLeaseCount, 1U);
    EXPECT_EQ(lease->surface(), *surface);

    Integration::NativeWindowSurfaceLease moved = std::move(*lease);
    EXPECT_FALSE(lease->hasValue());
    EXPECT_TRUE(moved.hasValue());
    EXPECT_EQ(control->activeLeaseCount, 1U);

    auto decoded = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(moved);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->nativeWindow, 1U);
    EXPECT_EQ(decoded->bindingRevision, 1U);

    moved = {};
    EXPECT_EQ(control->activeLeaseCount, 0U);
}

TEST(WindowSurfaceLeaseTest, RejectsDuplicateAndWrongThreadAcquisitionWithoutChangingPinCount)
{
    auto pool = createSurfacePool();
    auto surface = pool.tryEmplace();
    ASSERT_TRUE(surface.has_value());

    auto control = std::make_shared<Integration::Detail::NativeWindowSurfaceLeaseControl>();
    control->ownerThread = std::this_thread::get_id();
    auto first = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(control, *surface, fakeBinding());
    ASSERT_TRUE(first.has_value());

    auto duplicate = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(control, *surface, fakeBinding());
    EXPECT_FALSE(duplicate.has_value());
    EXPECT_EQ(control->activeLeaseCount, 1U);

    first = {};
    ASSERT_EQ(control->activeLeaseCount, 0U);

    bool wrongThreadRejected = false;
    std::thread otherThread([&] {
        auto wrongThread =
            Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(control, *surface, fakeBinding());
        wrongThreadRejected = !wrongThread.has_value();
    });
    otherThread.join();
    EXPECT_TRUE(wrongThreadRejected);
    EXPECT_EQ(control->activeLeaseCount, 0U);
}

TEST(WindowSurfaceLeaseDeathTest, DestroyingLeaseOffOwnerThreadTerminates)
{
    EXPECT_DEATH(
        {
            auto pool = createSurfacePool();
            auto surface = pool.tryEmplace();
            if (!surface.has_value())
            {
                std::abort();
            }

            auto control = std::make_shared<Integration::Detail::NativeWindowSurfaceLeaseControl>();
            control->ownerThread = std::this_thread::get_id();
            auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(control, *surface, fakeBinding());
            if (!lease.has_value())
            {
                std::abort();
            }

            std::thread wrongThread([lease = std::move(*lease)]() mutable { lease = {}; });
            wrongThread.join();
        },
        ".*");
}

} // namespace Tina::Tests
