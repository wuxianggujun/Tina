#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/FrameResource.hpp>

namespace Tina::Asset {

// Borrowed, allocation-free seam from a weak AssetHandle to a packet-local
// render resource. The callback, userData, and sink are valid only for the
// current extraction operation; this value owns none of them. Missing or
// unresolved assets return a successful empty ref. Errors are reserved for
// structural failures such as wrong-thread access or sink rejection.
struct AssetFrameResourceResolver final {
    using ResolveFn = Core::Result<Render::FrameResourceRef> (*)(
        void* userData,
        AssetHandle asset,
        Render::FrameResourceSink& sink) noexcept;

    void* userData = nullptr;
    ResolveFn resolve = nullptr;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return resolve != nullptr;
    }

    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    operator()(AssetHandle asset, Render::FrameResourceSink& sink) const noexcept
    {
        if (resolve == nullptr)
        {
            return Render::FrameResourceRef{};
        }
        return resolve(userData, asset, sink);
    }
};

} // namespace Tina::Asset
