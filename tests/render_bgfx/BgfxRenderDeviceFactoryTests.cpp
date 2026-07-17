#include "BgfxRenderDevice.hpp"
#include "BgfxSurfaceFramePlanner.hpp"
#include "WindowSurfaceLeaseAccess.hpp"

#include <tina/core/id/GenerationPool.hpp>
#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

struct TestSurfaceRecord final {};
using TestSurfacePool = Core::GenerationPool<TestSurfaceRecord, Integration::WindowSurfaceRegistryTag>;

[[nodiscard]] TestSurfacePool createSurfacePool()
{
    auto pool = TestSurfacePool::Create(2);
    EXPECT_TRUE(pool.has_value());
    return std::move(*pool);
}

[[nodiscard]] RenderSurfaceState activeSurface(Integration::WindowSurfaceId surface) noexcept
{
    return RenderSurfaceState{
        .surface =
            {
                .owner = surface.owner().value(),
                .index = surface.index(),
                .generation = surface.generation(),
            },
        .framebufferExtent = {640U, 480U},
        .contentScale = {1.0F, 1.0F},
        .sourceMetricsRevision = 1U,
        .surfaceRevision = 1U,
        .availability = RenderSurfaceAvailability::Active,
    };
}

[[nodiscard]] std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> createLeaseControl()
{
    auto control = std::make_shared<Integration::Detail::NativeWindowSurfaceLeaseControl>();
    control->ownerThread = std::this_thread::get_id();
    return control;
}

[[nodiscard]] Integration::Detail::NativeWindowBinding validWin32Binding() noexcept
{
    return Integration::Detail::NativeWindowBinding{
        .kind = Integration::Detail::NativeWindowBindingKind::Win32,
        .nativeDisplay = 0,
        .nativeWindow = 1,
        .bindingRevision = 1,
    };
}

TEST(BgfxRenderDeviceFactoryTest, RejectsMissingInitialSurfaceOrLeaseBeforeBgfxInitialization)
{
    auto noSurface = createBgfxRenderDevice(RenderDeviceCreateParams{}, {});
    ASSERT_FALSE(noSurface.has_value());
    EXPECT_EQ(noSurface.error().code, RenderErrorCode::InvalidSurfaceState);

    auto pool = createSurfacePool();
    auto surface = pool.tryEmplace();
    ASSERT_TRUE(surface.has_value());

    auto noLease = createBgfxRenderDevice(
        RenderDeviceCreateParams{.initialPrimaryWindowSurface = activeSurface(*surface)}, {});
    ASSERT_FALSE(noLease.has_value());
    EXPECT_EQ(noLease.error().code, RenderErrorCode::InvalidNativeWindowBinding);
}

TEST(BgfxRenderDeviceFactoryTest, SurfaceIdentityMismatchReleasesTheConsumedLease)
{
    auto pool = createSurfacePool();
    auto leaseSurface = pool.tryEmplace();
    auto frameSurface = pool.tryEmplace();
    ASSERT_TRUE(leaseSurface.has_value());
    ASSERT_TRUE(frameSurface.has_value());

    auto control = createLeaseControl();
    auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(
        control, *leaseSurface, validWin32Binding());
    ASSERT_TRUE(lease.has_value());
    ASSERT_EQ(control->activeLeaseCount, 1U);

    auto result = createBgfxRenderDevice(
        RenderDeviceCreateParams{.initialPrimaryWindowSurface = activeSurface(*frameSurface)},
        std::move(*lease));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidSurfaceState);
    EXPECT_EQ(control->activeLeaseCount, 0U);
}

TEST(BgfxRenderDeviceFactoryTest, UnsupportedNativeBindingReleasesTheConsumedLease)
{
    auto pool = createSurfacePool();
    auto surface = pool.tryEmplace();
    ASSERT_TRUE(surface.has_value());

    auto control = createLeaseControl();
    auto unsupportedBinding = validWin32Binding();
    unsupportedBinding.kind = static_cast<Integration::Detail::NativeWindowBindingKind>(255);
    auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(
        control, *surface, unsupportedBinding);
    ASSERT_TRUE(lease.has_value());

    auto result = createBgfxRenderDevice(
        RenderDeviceCreateParams{.initialPrimaryWindowSurface = activeSurface(*surface)},
        std::move(*lease));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidNativeWindowBinding);
    EXPECT_EQ(control->activeLeaseCount, 0U);
}

TEST(BgfxRenderDeviceFactoryTest, OversizedInitialViewReleasesTheConsumedLease)
{
    auto pool = createSurfacePool();
    auto surface = pool.tryEmplace();
    ASSERT_TRUE(surface.has_value());

    auto control = createLeaseControl();
    auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(
        control, *surface, validWin32Binding());
    ASSERT_TRUE(lease.has_value());

    auto initialSurface = activeSurface(*surface);
    initialSurface.framebufferExtent.width = BgfxSurfaceFramePlanner::MaxViewRectExtent + 1U;
    auto result = createBgfxRenderDevice(
        RenderDeviceCreateParams{.initialPrimaryWindowSurface = initialSurface},
        std::move(*lease));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::SurfaceReconfigureFailed);
    EXPECT_EQ(control->activeLeaseCount, 0U);
}

} // namespace
} // namespace Tina::Render::Bgfx
