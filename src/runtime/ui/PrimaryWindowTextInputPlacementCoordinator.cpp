#include "PrimaryWindowTextInputPlacementCoordinator.hpp"

#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIPublicationPipeline.hpp>

#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

namespace Tina::Runtime::Detail {
namespace {

[[nodiscard]] Core::Status lifecycleFailure(std::string_view message,
                                            std::string_view detail = {})
{
    Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation, message};
    error.addContext("PrimaryWindowTextInputPlacementCoordinator::publish", detail);
    return Core::failure(std::move(error));
}

} // namespace

PrimaryWindowTextInputPlacementCoordinator::PrimaryWindowTextInputPlacementCoordinator() noexcept
    : ownerThreadId_(std::this_thread::get_id())
{
}

Core::Status PrimaryWindowTextInputPlacementCoordinator::publish(
    UI::UIContext* context, Platform::IPlatformBackend& backend)
{
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "Text input placement must be published on its owner thread");
    }

    std::optional<Platform::TextInputPlacement> placement;
    if (context != nullptr)
    {
        const std::optional<UI::UILogicalRect> caret = context->publication().committedTextInputCaretRect();
        if (caret.has_value())
        {
            if (!std::isfinite(caret->x) || !std::isfinite(caret->y) ||
                !std::isfinite(caret->width) || !std::isfinite(caret->height) ||
                caret->width < 0.0F || caret->height <= 0.0F)
            {
                return lifecycleFailure("The committed TextEdit caret geometry is invalid");
            }
            placement = Platform::TextInputPlacement{
                .window = context->ownerWindow(),
                .caret = Platform::TextInputCaretRect{
                    .x = static_cast<double>(caret->x),
                    .y = static_cast<double>(caret->y),
                    .width = static_cast<double>(caret->width),
                    .height = static_cast<double>(caret->height),
                },
            };
        }
    }

    Core::Status status = backend.updateTextInputPlacement(std::move(placement));
    if (!status)
    {
        Core::Error error = std::move(status.error());
        error.addContext("PrimaryWindowTextInputPlacementCoordinator::publish",
                         "IPlatformBackend::updateTextInputPlacement");
        return Core::failure(std::move(error));
    }
    return Core::success();
}

} // namespace Tina::Runtime::Detail
