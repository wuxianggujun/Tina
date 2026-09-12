#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/android/AndroidPlatformFactory.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/runtime/EngineHost.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace Tina::Android {

struct CreateEngineOptions final {
    // The product acquires and retains the native window. No JNI or NDK type
    // crosses this boundary. The platform field is filled from EngineConfig.
    Platform::AndroidPlatformBackendCreateParams window{};
    Render::RendererApi rendererApi = Render::RendererApi::Automatic;
    std::shared_ptr<std::vector<std::byte>> uiFontBytes{};
    std::shared_ptr<std::vector<std::byte>> uiFontAtlasBytes{};
    std::vector<std::shared_ptr<std::vector<std::byte>>> uiFallbackFontBytes{};
};

struct EngineInstance final {
    std::unique_ptr<EngineHost> host{};
    // Borrowed lifecycle facet, valid only while host owns a live backend.
    // Do not use after host->stop()/a terminal tick, or after resetting host.
    Platform::IAndroidPlatformBackend* platform = nullptr;
};

// Installed-SDK Android composition: clock + bounded tasks + Android surface
// backend + bgfx + optional FreeType UI. Products implement IGameApplication and
// drive host->start()/tick()/stop() on the creating thread (e.g. Choreographer).
// The caller must retain every ANativeWindow used by the render thread until the
// replacement binding is observed, and destroy the host before releasing windows.
[[nodiscard]] Core::Result<EngineInstance> CreateEngine(
    const EngineConfig& config, CreateEngineOptions options) noexcept;

} // namespace Tina::Android
