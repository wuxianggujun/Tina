#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/FrameResource.hpp>

#include <optional>

namespace Tina::Render {

struct Texture2DFrameResourceResolution final {
    FrameResourceRef resource{};
    u32 pixelWidth = 0;
    u32 pixelHeight = 0;
};

// Backend-neutral resolver callback. A successful empty result means the asset
// is missing or not ready this frame; failures are structural extraction errors.
struct Texture2DFrameResourceResolver final {
    using ResolveFn = Core::Result<std::optional<Texture2DFrameResourceResolution>> (*)(
        void* userData, Core::AssetId asset, FrameResourceSink& sink) noexcept;

    void* userData = nullptr;
    ResolveFn resolve = nullptr;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return resolve != nullptr;
    }
};

} // namespace Tina::Render
