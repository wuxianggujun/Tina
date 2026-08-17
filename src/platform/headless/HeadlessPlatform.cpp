#include <tina/platform/PlatformErrors.hpp>
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

    void shutdown() noexcept override
    {
        stopped_ = true;
    }

  private:
    PlatformFrameBuilder frameBuilder_;
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
