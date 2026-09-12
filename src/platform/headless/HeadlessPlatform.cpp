#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/ProcessLocalClipboard.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>

#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace Tina::Platform {
namespace {

class HeadlessPlatformBackend final : public IPlatformBackend {
  public:
    explicit HeadlessPlatformBackend(PlatformFrameBuilder frameBuilder) noexcept
        : frameBuilder_(std::move(frameBuilder))
    {
    }

    [[nodiscard]] Core::Result<std::optional<WindowMetricsSnapshot>> initialPrimaryWindowMetrics() override
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The headless platform backend is stopped");
        }
        return std::optional<WindowMetricsSnapshot>{};
    }

    [[nodiscard]] Core::Result<PlatformPollResult> pollFrame() override
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The headless platform backend is stopped");
        }

        if (nextFrameId_ == (std::numeric_limits<u64>::max)())
        {
            return Core::failure(PlatformErrorCode::FrameSequenceExhausted,
                                 "The headless platform frame sequence is exhausted");
        }

        auto beginStatus = frameBuilder_.beginFrame(PlatformFrameId{nextFrameId_++});
        if (!beginStatus.has_value())
        {
            return std::unexpected(std::move(beginStatus.error()));
        }
        auto frame = frameBuilder_.finishFrame();
        if (!frame.has_value())
        {
            return std::unexpected(std::move(frame.error()));
        }
        return PlatformPollResult::Continue(*frame);
    }

    Core::Status updateTextInputPlacement(std::optional<TextInputPlacement> placement) override
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The headless platform backend is stopped");
        }
        // Headless has no native text-input surface. A non-null placement is a
        // caller contract error rather than a silently retained compatibility
        // path; clearing remains a successful no-op.
        if (placement.has_value())
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Headless platform cannot publish native text input placement");
        }
        return Core::success();
    }

    Core::Status setPointerCaptureMode(PointerCaptureMode mode) override
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The headless platform backend is stopped");
        }
        // Headless has no window or cursor: Free is its permanent state, and Locked
        // is a caller contract error since the capability cannot exist.
        if (mode != PointerCaptureMode::Free)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Headless platform cannot lock a cursor");
        }
        return Core::success();
    }

    [[nodiscard]] IClipboard* clipboard() noexcept override
    {
        // Headless has no OS clipboard, but the capability is still real: copy and
        // paste are process-local here. Returning nullptr would push every test
        // that exercises those paths into supplying its own fake, so the code
        // under test would differ per test. The stored text is never visible
        // outside this process.
        return &clipboard_;
    }

    [[nodiscard]] ISoftKeyboard* softKeyboard() noexcept override
    {
        // Headless has no soft keyboard capability.
        return nullptr;
    }

    void shutdown() noexcept override
    {
        stopped_ = true;
        // No fence needed as in GLFW: this clipboard owns plain memory and
        // touches no library that shutdown invalidates.
    }

  private:
    PlatformFrameBuilder frameBuilder_;
    ProcessLocalClipboard clipboard_{};
    u64 nextFrameId_ = 1;
    bool stopped_ = false;
};

} // namespace

Core::Result<std::unique_ptr<IPlatformBackend>> createHeadlessPlatformBackend(const PlatformBackendCreateParams& params)
{
    auto frameBuilder = PlatformFrameBuilder::Create(params.frameCapacities);
    if (!frameBuilder.has_value())
    {
        return std::unexpected(std::move(frameBuilder.error()));
    }

    std::unique_ptr<IPlatformBackend> backend = std::make_unique<HeadlessPlatformBackend>(std::move(*frameBuilder));
    return backend;
}

} // namespace Tina::Platform
