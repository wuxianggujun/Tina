#pragma once

// Reaching the live IRenderDevice from a sample. CreateEngineOptions::wrapWindowSurfaceRenderDevice
// is the only hook that sees it, and it hands over ownership.
//
// The hook returns the device unchanged and keeps a raw pointer beside it. A
// forwarding wrapper would be worse than redundant here: every optional
// IRenderDevice entry point has a base implementation that fails with
// "unsupported", so a wrapper that forgets to forward one call turns into a
// runtime upload failure rather than a compile error. Identity capture cannot
// have that bug. src/desktop/DesktopEngine.cpp treats the hook's result as
// opaque and never compares it against the input.
//
// Lifetime: EngineModules owns the device and resets it only after every
// IGameState::onExit has run, so the captured pointer is valid for the whole
// state lifetime. It dangles after the host is destroyed, so nothing may touch
// it from a destructor that outlives the host.

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <memory>
#include <utility>

namespace VoxelSample {

class DeviceCapture final {
  public:
    void set(Tina::Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Tina::Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Tina::Render::IRenderDevice* device_ = nullptr;
};

[[nodiscard]] inline Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>>
captureRenderDevice(std::unique_ptr<Tina::Render::IRenderDevice> device, DeviceCapture& capture)
{
    if (!device)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Desktop bootstrap produced no render device");
    }
    capture.set(device.get());
    return device;
}

} // namespace VoxelSample
