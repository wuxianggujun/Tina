#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderErrors.hpp>

#include <exception>
#include <utility>

namespace Tina::Samples {

// Lifetime owner for fixed Sprite2D bindings used by self-contained samples.
// EngineHost releases every packet pin before the owning GameState is destroyed.
class SampleSpriteFrameResource final {
  public:
    SampleSpriteFrameResource() noexcept = default;
    SampleSpriteFrameResource(const SampleSpriteFrameResource&) = delete;
    SampleSpriteFrameResource& operator=(const SampleSpriteFrameResource&) = delete;

    ~SampleSpriteFrameResource() noexcept
    {
        if (m_frameBorrowCount != 0)
        {
            std::terminate();
        }
    }

    [[nodiscard]] Core::Result<Render::FrameResourceRef> intern(
        Render::FrameResourceSink& sink,
        Core::u64 deviceBindingKey) const noexcept
    {
        if (deviceBindingKey == 0)
        {
            return Core::failure(Render::RenderErrorCode::InvalidFrameResource,
                                 "sample Sprite2D binding key must be non-zero");
        }

        ++m_frameBorrowCount;
        Render::FramePin pin{
            Render::FramePinKind::Custom,
            deviceBindingKey,
            const_cast<SampleSpriteFrameResource*>(this),
            &SampleSpriteFrameResource::releaseFrameBorrow,
        };
        return sink.intern(
            Render::FrameResourceDescriptor{
                .kind = Render::FrameResourceKind::Sprite2DTexture,
                .deviceBindingKey = deviceBindingKey,
            },
            std::move(pin));
    }

    [[nodiscard]] Core::u32 frameBorrowCount() const noexcept { return m_frameBorrowCount; }

  private:
    static void releaseFrameBorrow(void* userData) noexcept
    {
        auto* owner = static_cast<SampleSpriteFrameResource*>(userData);
        if (owner == nullptr || owner->m_frameBorrowCount == 0)
        {
            std::terminate();
        }
        --owner->m_frameBorrowCount;
    }

    mutable Core::u32 m_frameBorrowCount = 0;
};

} // namespace Tina::Samples
