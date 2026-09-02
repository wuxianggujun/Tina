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

TEST(BgfxRenderDeviceFactoryTest, RejectsInvalidShadowMapExtentsBeforeSurfaceValidation)
{
    RenderDeviceCreateParams params{};
    params.shadowMapExtents.spotLightMapExtent = 384;

    auto result = createBgfxRenderDevice(params, {});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidShadowMapExtentConfig);
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

// ANativeWindow* is self-contained, so a display pointer on an Android binding would be
// silently dropped by bgfx. Rejecting it keeps the binding's fields meaningful rather than
// accepting one whose extra field does nothing.
TEST(BgfxRenderDeviceFactoryTest, AndroidBindingRejectsADisplayPointerAndReleasesTheLease)
{
    auto pool = createSurfacePool();
    auto surface = pool.tryEmplace();
    ASSERT_TRUE(surface.has_value());

    auto control = createLeaseControl();
    auto androidBinding = validWin32Binding();
    androidBinding.kind = Integration::Detail::NativeWindowBindingKind::Android;
    androidBinding.nativeDisplay = 0x1234;
    auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(
        control, *surface, androidBinding);
    ASSERT_TRUE(lease.has_value());

    auto result = createBgfxRenderDevice(
        RenderDeviceCreateParams{.initialPrimaryWindowSurface = activeSurface(*surface)},
        std::move(*lease));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidNativeWindowBinding);
    EXPECT_EQ(control->activeLeaseCount, 0U);
}

// A missing window handle is rejected by the lease itself, one layer before bgfx decoding, so
// Android inherits that guard with no extra code. Asserting it here pins where the check
// lives: a null ANativeWindow* can never reach bgfx::init, and no lease is leaked either.
TEST(BgfxRenderDeviceFactoryTest, AndroidBindingWithoutAWindowHandleIsRejectedByTheLease)
{
    auto pool = createSurfacePool();
    auto surface = pool.tryEmplace();
    ASSERT_TRUE(surface.has_value());

    auto control = createLeaseControl();
    auto androidBinding = validWin32Binding();
    androidBinding.kind = Integration::Detail::NativeWindowBindingKind::Android;
    androidBinding.nativeWindow = 0;

    auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(
        control, *surface, androidBinding);
    ASSERT_FALSE(lease.has_value());
    EXPECT_EQ(control->activeLeaseCount, 0U);
}

// An HTML5 binding carries a CSS selector in nativeWindow, not a handle, and there is no
// display to go with it. A display pointer would be silently ignored by bgfx's HTML5 context,
// so it is rejected for the same reason as on Android: an accepted field that does nothing is
// worse than a rejected binding.
TEST(BgfxRenderDeviceFactoryTest, Html5BindingRejectsADisplayPointerAndReleasesTheLease)
{
    auto pool = createSurfacePool();
    auto surface = pool.tryEmplace();
    ASSERT_TRUE(surface.has_value());

    auto control = createLeaseControl();
    auto html5Binding = validWin32Binding();
    html5Binding.kind = Integration::Detail::NativeWindowBindingKind::Html5;
    html5Binding.nativeDisplay = 0x1234;
    auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(
        control, *surface, html5Binding);
    ASSERT_TRUE(lease.has_value());

    auto result = createBgfxRenderDevice(
        RenderDeviceCreateParams{.initialPrimaryWindowSurface = activeSurface(*surface)},
        std::move(*lease));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidNativeWindowBinding);
    EXPECT_EQ(control->activeLeaseCount, 0U);
}

// A null selector pointer is rejected by the lease, one layer before bgfx decoding. Without
// this guard bgfx would call bx::strCopy on nullptr rather than fail a check.
TEST(BgfxRenderDeviceFactoryTest, Html5BindingWithoutASelectorIsRejectedByTheLease)
{
    auto pool = createSurfacePool();
    auto surface = pool.tryEmplace();
    ASSERT_TRUE(surface.has_value());

    auto control = createLeaseControl();
    auto html5Binding = validWin32Binding();
    html5Binding.kind = Integration::Detail::NativeWindowBindingKind::Html5;
    html5Binding.nativeWindow = 0;

    auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(
        control, *surface, html5Binding);
    ASSERT_FALSE(lease.has_value());
    EXPECT_EQ(control->activeLeaseCount, 0U);
}

// The switch over binding kinds is exhaustive so that adding an enumerator breaks the build;
// a value that is not an enumerator at all is caught by an explicit range check instead.
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

TEST(BgfxRenderDeviceFactoryTest, VsyncDefaultsOnAndIRenderDeviceProvidesADefaultImplementation)
{
    // A real bgfx device needs a window, so the backend toggle itself is covered
    // by the null device and by running the Editor. What must hold here is that
    // the create param defaults to on, so adding it cannot silently change the
    // pixel-evidence gates.
    const RenderDeviceCreateParams params{};
    EXPECT_TRUE(params.vsync);

    // Devices predating the setting inherit a default implementation rather than
    // failing to compile. The default reports vsync on and ignores writes.
    class LegacyDevice final : public IRenderDevice {
      public:
        [[nodiscard]] Core::Result<RenderFrameSubmission> submitFrame(const RenderFrame& frame) override
        {
            static_cast<void>(frame);
            return Core::failure(RenderErrorCode::DeviceInitializationFailed, "not used");
        }
        [[nodiscard]] Core::Status present() override
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed, "not used");
        }
        [[nodiscard]] RenderStatistics statistics() const noexcept override
        {
            return {};
        }
        void shutdown() noexcept override {}
    };

    LegacyDevice legacy;
    EXPECT_TRUE(legacy.vsyncEnabled());
    legacy.setVsyncEnabled(false);
    EXPECT_TRUE(legacy.vsyncEnabled())
        << "the default implementation must absorb the request, not pretend it applied";
}

} // namespace
} // namespace Tina::Render::Bgfx
