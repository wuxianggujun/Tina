#include <tina/ui/UISnackbar.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Tina::UI {
namespace {

[[nodiscard]] bool validDuration(Core::Duration duration, bool allowZero) noexcept
{
    return std::isfinite(duration.count()) &&
           (allowZero ? duration.count() >= 0.0 : duration.count() > 0.0) &&
           duration.count() <= 60.0;
}

[[nodiscard]] bool validTone(UISnackbarTone tone) noexcept
{
    return tone >= UISnackbarTone::Neutral && tone <= UISnackbarTone::Error;
}

} // namespace

Core::Result<UISnackbarHost>
UISnackbarHost::Create(const UISnackbarHostConfig& config) noexcept
{
    if (config.queueCapacity == 0U ||
        config.queueCapacity > UISnackbarMaximumQueueCapacity ||
        !validDuration(config.enterDuration, true) ||
        !validDuration(config.exitDuration, true) ||
        !validDuration(config.defaultVisibleDuration, false) ||
        !std::isfinite(config.viewportMargin) || config.viewportMargin < 0.0F) {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UI Snackbar host configuration is invalid");
    }
    return UISnackbarHost{config};
}

Core::Status UISnackbarHost::enqueue(
    const UISnackbarMessage& message, Core::MonotonicTimePoint now) noexcept
{
    if (message.text.empty() ||
        message.text.size() > UISnackbarMaximumMessageBytes ||
        !Core::isStrictUtf8WithoutNul(message.text) || !validTone(message.tone)) {
        return Core::failure(UIErrorCode::InvalidText,
                             "UI Snackbar message must be non-empty bounded strict UTF-8");
    }
    const bool hasAction = message.actionLabel.has_value();
    if (hasAction && (message.actionLabel->empty() ||
                      message.actionLabel->size() > UISnackbarMaximumActionLabelBytes ||
                      !Core::isStrictUtf8WithoutNul(*message.actionLabel) ||
                      message.actionToken == 0U)) {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UI Snackbar action requires a bounded UTF-8 label and non-zero token");
    }
    if (!hasAction && message.actionToken != 0U) {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UI Snackbar action token requires an action label");
    }
    const Core::Duration visibleDuration =
        message.visibleDuration.value_or(config_.defaultVisibleDuration);
    if (!validDuration(visibleDuration, false)) {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UI Snackbar visible duration is invalid");
    }
    if (count_ >= config_.queueCapacity) {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI Snackbar queue capacity has been exhausted");
    }

    StoredMessage& stored = queue_[(head_ + count_) % config_.queueCapacity];
    stored = {};
    std::memcpy(stored.text.data(), message.text.data(), message.text.size());
    stored.textBytes = message.text.size();
    if (hasAction) {
        std::memcpy(stored.actionLabel.data(), message.actionLabel->data(),
                    message.actionLabel->size());
        stored.actionLabelBytes = message.actionLabel->size();
    }
    stored.actionToken = message.actionToken;
    stored.tone = message.tone;
    stored.visibleDuration = visibleDuration;
    ++count_;
    if (count_ == 1U) {
        beginFront(now);
    }
    return Core::success();
}

bool UISnackbarHost::update(Core::MonotonicTimePoint now) noexcept
{
    if (phase_ == UISnackbarPhase::Hidden || count_ == 0U) {
        return false;
    }
    const Core::Duration elapsed = Core::durationBetween(phaseStarted_, now);
    if (phase_ == UISnackbarPhase::Entering &&
        elapsed >= config_.enterDuration) {
        phase_ = UISnackbarPhase::Visible;
        phaseStarted_ = now;
        advanceRevision();
        return true;
    }
    const StoredMessage* current = front();
    if (phase_ == UISnackbarPhase::Visible && current != nullptr &&
        elapsed >= current->visibleDuration) {
        beginExit(now);
        return true;
    }
    if (phase_ == UISnackbarPhase::Exiting &&
        elapsed >= config_.exitDuration) {
        popFront(now);
        return true;
    }
    return false;
}

void UISnackbarHost::dismiss(Core::MonotonicTimePoint now) noexcept
{
    if (phase_ == UISnackbarPhase::Entering ||
        phase_ == UISnackbarPhase::Visible) {
        beginExit(now);
    }
}

std::optional<u64>
UISnackbarHost::activateAction(Core::MonotonicTimePoint now) noexcept
{
    const StoredMessage* current = front();
    if (current == nullptr || current->actionToken == 0U ||
        current->actionLabelBytes == 0U) {
        return std::nullopt;
    }
    const u64 token = current->actionToken;
    beginExit(now);
    return token;
}

UISnackbarPresentation UISnackbarHost::presentation() const noexcept
{
    const StoredMessage* current = front();
    if (current == nullptr) {
        return {.revision = revision_, .phase = UISnackbarPhase::Hidden};
    }
    return {
        .text = std::string_view{current->text.data(), current->textBytes},
        .actionLabel = std::string_view{current->actionLabel.data(),
                                        current->actionLabelBytes},
        .actionToken = current->actionToken,
        .revision = revision_,
        .tone = current->tone,
        .phase = phase_,
    };
}

UISnackbarHost::StoredMessage* UISnackbarHost::front() noexcept
{
    return count_ == 0U ? nullptr : &queue_[head_];
}

const UISnackbarHost::StoredMessage* UISnackbarHost::front() const noexcept
{
    return count_ == 0U ? nullptr : &queue_[head_];
}

void UISnackbarHost::beginFront(Core::MonotonicTimePoint now) noexcept
{
    phase_ = UISnackbarPhase::Entering;
    phaseStarted_ = now;
    advanceRevision();
}

void UISnackbarHost::beginExit(Core::MonotonicTimePoint now) noexcept
{
    phase_ = UISnackbarPhase::Exiting;
    phaseStarted_ = now;
    advanceRevision();
}

void UISnackbarHost::popFront(Core::MonotonicTimePoint now) noexcept
{
    if (count_ == 0U) {
        return;
    }
    queue_[head_] = {};
    head_ = (head_ + 1U) % config_.queueCapacity;
    --count_;
    if (count_ == 0U) {
        phase_ = UISnackbarPhase::Hidden;
        advanceRevision();
        return;
    }
    beginFront(now);
}

void UISnackbarHost::advanceRevision() noexcept
{
    ++revision_;
    if (revision_ == 0U) {
        revision_ = 1U;
    }
}

Core::Result<UIComponentBuildBudget>
requiredSnackbarHostBuildBudget(const UISnackbarHostConfig& config) noexcept
{
    auto host = UISnackbarHost::Create(config);
    if (!host) {
        return Core::failure(host.error());
    }
    return UIComponentBuildBudget{
        .nodes = 5U,
        .textBytes = 6U,
        .behaviors = {.activate = 1U},
    };
}

} // namespace Tina::UI
