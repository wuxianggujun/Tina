#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>

#include <memory>

namespace Tina::Platform {
namespace {

class HeadlessPlatformBackend final : public IPlatformBackend {
public:
    [[nodiscard]] Core::Result<PlatformPollResult> pollEvents() override
    {
        if (stopped_) {
            return Core::failure(
                PlatformErrorCode::BackendStopped,
                "The headless platform backend is stopped");
        }

        return PlatformPollResult{};
    }

    void shutdown() noexcept override
    {
        stopped_ = true;
    }

private:
    bool stopped_ = false;
};

} // namespace

Core::Result<std::unique_ptr<IPlatformBackend>> createHeadlessPlatformBackend(
    const PlatformBackendCreateParams& params)
{
    static_cast<void>(params);

    std::unique_ptr<IPlatformBackend> backend = std::make_unique<HeadlessPlatformBackend>();
    return backend;
}

} // namespace Tina::Platform
