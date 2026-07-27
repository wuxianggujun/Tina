#include "PrimaryWindowUICapabilityState.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <limits>
#include <string_view>
#include <utility>

namespace Tina::Runtime::Detail {
namespace {

[[nodiscard]] Core::Error capabilityError(Core::ErrorCode code, std::string_view message, std::string_view operation)
{
    Core::Error error{code, message};
    error.addContext(operation);
    return error;
}

} // namespace

PrimaryWindowUICapabilityState::PrimaryWindowUICapabilityState() noexcept : ownerThreadId_(std::this_thread::get_id())
{
}

Core::Result<u64> PrimaryWindowUICapabilityState::beginGameStateEnterPhase(UI::UIContext* context)
{
    return beginPhase(PrimaryWindowUIPhase::GameStateEnter, context);
}

Core::Result<u64> PrimaryWindowUICapabilityState::beginUIUpdatePhase(UI::UIContext* context)
{
    return beginPhase(PrimaryWindowUIPhase::UIUpdate, context);
}

Core::Result<u64> PrimaryWindowUICapabilityState::beginPhase(PrimaryWindowUIPhase phase, UI::UIContext* context)
{
    constexpr std::string_view Operation = "PrimaryWindowUICapabilityState::beginPhase";
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(capabilityError(RuntimeErrorCode::WrongOwnerThread,
                                             "Primary-window UI phases may begin only on the Runtime owner thread",
                                             Operation));
    }
    if (phase == PrimaryWindowUIPhase::None || phase_ != PrimaryWindowUIPhase::None)
    {
        return Core::failure(capabilityError(RuntimeErrorCode::LifecycleInvariantViolation,
                                             "Primary-window UI phases cannot be empty or nested", Operation));
    }
    if (epoch_ == (std::numeric_limits<u64>::max)())
    {
        return Core::failure(capabilityError(RuntimeErrorCode::LifecycleInvariantViolation,
                                             "The primary-window UI phase epoch is exhausted", Operation));
    }

    ++epoch_;
    phase_ = phase;
    context_ = context;
    firstError_.reset();
    return epoch_;
}

Core::Status PrimaryWindowUICapabilityState::finishPhase(u64 epoch, PrimaryWindowUIPhase phase)
{
    constexpr std::string_view Operation = "PrimaryWindowUICapabilityState::finishPhase";
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(capabilityError(RuntimeErrorCode::WrongOwnerThread,
                                             "Primary-window UI phases may finish only on the Runtime owner thread",
                                             Operation));
    }
    if (epoch == 0 || epoch != epoch_ || phase == PrimaryWindowUIPhase::None || phase != phase_)
    {
        return Core::failure(capabilityError(RuntimeErrorCode::LifecycleInvariantViolation,
                                             "The primary-window UI phase finish does not match the active epoch",
                                             Operation));
    }

    std::optional<Core::Error> firstError = std::move(firstError_);
    firstError_.reset();
    context_ = nullptr;
    phase_ = PrimaryWindowUIPhase::None;
    if (firstError.has_value())
    {
        return Core::failure(std::move(*firstError));
    }
    return Core::success();
}

void PrimaryWindowUICapabilityState::abortPhase(u64 epoch, PrimaryWindowUIPhase phase) noexcept
{
    if (std::this_thread::get_id() != ownerThreadId_ || epoch == 0 || epoch != epoch_ ||
        phase == PrimaryWindowUIPhase::None || phase != phase_)
    {
        return;
    }

    firstError_.reset();
    context_ = nullptr;
    phase_ = PrimaryWindowUIPhase::None;
}

bool PrimaryWindowUICapabilityState::hasPrimaryWindowUI(u64 epoch, PrimaryWindowUIPhase phase) const noexcept
{
    return std::this_thread::get_id() == ownerThreadId_ && epoch != 0 && epoch == epoch_ &&
           phase != PrimaryWindowUIPhase::None && phase == phase_ && context_ != nullptr;
}

Core::Result<UI::UICommittedSemanticsView> PrimaryWindowUICapabilityState::committedSemantics(
    u64 epoch, PrimaryWindowUIPhase phase)
{
    constexpr std::string_view Operation = "PrimaryWindowUICapabilityState::committedSemantics";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return context_->committedSemantics();
}

Core::Status PrimaryWindowUICapabilityState::validate(u64 epoch, PrimaryWindowUIPhase phase, bool requireContext,
                                                      std::string_view operation)
{
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(capabilityError(RuntimeErrorCode::WrongOwnerThread,
                                             "Primary-window UI capabilities are owner-thread only", operation));
    }
    if (epoch == 0 || epoch != epoch_ || phase == PrimaryWindowUIPhase::None || phase != phase_)
    {
        return Core::failure(capabilityError(RuntimeErrorCode::UIPhaseCapabilityExpired,
                                             "The primary-window UI capability has expired", operation));
    }
    if (firstError_.has_value())
    {
        return Core::failure(*firstError_);
    }
    if (requireContext && context_ == nullptr)
    {
        return Core::failure(rememberFirstError(Core::Error{RuntimeErrorCode::PrimaryWindowUIUnavailable,
                                                            "The active Runtime phase has no primary-window UI"},
                                                operation));
    }
    return Core::success();
}

Core::Error PrimaryWindowUICapabilityState::rememberFirstError(Core::Error error, std::string_view operation)
{
    if (!firstError_.has_value())
    {
        error.addContext(operation);
        firstError_.emplace(std::move(error));
    }
    return *firstError_;
}

Core::Result<PrimaryWindowUIRootBuilder> PrimaryWindowUICapabilityState::rootBuilder(u64 epoch)
{
    constexpr std::string_view Operation = "GameStateEnterContext::primaryWindowUIRootBuilder";
    if (Core::Status status = validate(epoch, PrimaryWindowUIPhase::GameStateEnter, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return PrimaryWindowUIRootBuilder{*this, epoch};
}

Core::Result<PrimaryWindowUITreeUpdater>
PrimaryWindowUICapabilityState::treeUpdater(u64 epoch, PrimaryWindowUIPhase phase, UI::UIRootOwner& rootOwner)
{
    constexpr std::string_view Operation = "PrimaryWindowUICapabilityState::treeUpdater";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto updater = context_->treeUpdater(rootOwner);
    if (!updater)
    {
        return Core::failure(rememberFirstError(std::move(updater.error()), Operation));
    }
    return PrimaryWindowUITreeUpdater{*this, epoch, phase, std::move(*updater)};
}

Core::Result<UI::UIRootOwner> PrimaryWindowUICapabilityState::createRoot(u64 epoch)
{
    constexpr std::string_view Operation = "PrimaryWindowUIRootBuilder::createRoot";
    if (Core::Status status = validate(epoch, PrimaryWindowUIPhase::GameStateEnter, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto root = context_->rootBuilder().createRoot();
    if (!root)
    {
        return Core::failure(rememberFirstError(std::move(root.error()), Operation));
    }
    return std::move(*root);
}

Core::Result<bool> PrimaryWindowUICapabilityState::isAlive(u64 epoch, PrimaryWindowUIPhase phase,
                                                           const UI::UITreeUpdater& updater, UI::UINodeId node)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isAlive";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return updater.isAlive(node);
}

Core::Result<UI::UINodeId> PrimaryWindowUICapabilityState::createPanel(u64 epoch, PrimaryWindowUIPhase phase,
                                                                       UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::createPanel";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto child = updater.createPanel(parent);
    if (!child)
    {
        return Core::failure(rememberFirstError(std::move(child.error()), Operation));
    }
    return *child;
}

Core::Result<UI::UINodeId> PrimaryWindowUICapabilityState::createLabel(u64 epoch, PrimaryWindowUIPhase phase,
                                                                       UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::createLabel";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto child = updater.createLabel(parent);
    if (!child)
    {
        return Core::failure(rememberFirstError(std::move(child.error()), Operation));
    }
    return *child;
}

Core::Result<UI::UINodeId> PrimaryWindowUICapabilityState::createTextEdit(u64 epoch, PrimaryWindowUIPhase phase,
                                                                          UI::UITreeUpdater& updater,
                                                                          UI::UINodeId parent)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::createTextEdit";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto child = updater.createTextEdit(parent);
    if (!child)
    {
        return Core::failure(rememberFirstError(std::move(child.error()), Operation));
    }
    return *child;
}

Core::Result<UI::UINodeId> PrimaryWindowUICapabilityState::createButton(u64 epoch, PrimaryWindowUIPhase phase,
                                                                        UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::createButton";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto child = updater.createButton(parent);
    if (!child)
    {
        return Core::failure(rememberFirstError(std::move(child.error()), Operation));
    }
    return *child;
}

Core::Result<UI::UINodeId> PrimaryWindowUICapabilityState::createCheckbox(u64 epoch, PrimaryWindowUIPhase phase,
                                                                          UI::UITreeUpdater& updater,
                                                                          UI::UINodeId parent)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::createCheckbox";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto child = updater.createCheckbox(parent);
    if (!child)
    {
        return Core::failure(rememberFirstError(std::move(child.error()), Operation));
    }
    return *child;
}

Core::Result<UI::UINodeId> PrimaryWindowUICapabilityState::createSlider(u64 epoch, PrimaryWindowUIPhase phase,
                                                                        UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::createSlider";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto child = updater.createSlider(parent);
    if (!child)
    {
        return Core::failure(rememberFirstError(std::move(child.error()), Operation));
    }
    return *child;
}

Core::Result<UI::UINodeId> PrimaryWindowUICapabilityState::createProgressBar(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::createProgressBar";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto child = updater.createProgressBar(parent);
    if (!child)
    {
        return Core::failure(rememberFirstError(std::move(child.error()), Operation));
    }
    return *child;
}

Core::Result<UI::UINodeId> PrimaryWindowUICapabilityState::createRadioButton(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::createRadioButton";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto child = updater.createRadioButton(parent);
    if (!child)
    {
        return Core::failure(rememberFirstError(std::move(child.error()), Operation));
    }
    return *child;
}

Core::Status PrimaryWindowUICapabilityState::setLayoutStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                            UI::UITreeUpdater& updater, UI::UINodeId node,
                                                            const UI::UILayoutStyle& style)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setLayoutStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setLayoutStyle(node, style);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setPointerHitPolicy(u64 epoch, PrimaryWindowUIPhase phase,
                                                                 UI::UITreeUpdater& updater, UI::UINodeId node,
                                                                 UI::UIPointerHitPolicy policy)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setPointerHitPolicy";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setPointerHitPolicy(node, policy);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setEnabled(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId node, bool enabled)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setEnabled";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setEnabled(node, enabled);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<bool> PrimaryWindowUICapabilityState::isEnabled(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
    UI::UINodeId node)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isEnabled";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto enabled = updater.isEnabled(node);
    if (!enabled)
    {
        return Core::failure(rememberFirstError(std::move(enabled.error()), Operation));
    }
    return *enabled;
}

Core::Result<UI::UITheme> PrimaryWindowUICapabilityState::productTheme(
    u64 epoch,
    PrimaryWindowUIPhase phase)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::productTheme";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return context_->productTheme();
}

Core::Status PrimaryWindowUICapabilityState::setProductTheme(
    u64 epoch,
    PrimaryWindowUIPhase phase,
    const UI::UITheme& theme)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setProductTheme";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = context_->setProductTheme(theme);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setBoxPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                         UI::UITreeUpdater& updater, UI::UINodeId node,
                                                         const UI::UIBoxPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setBoxPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setBoxPaint(node, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setButtonPaint(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId button, const UI::UIButtonPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setButtonPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setButtonPaint(button, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIButtonPaint> PrimaryWindowUICapabilityState::buttonPaint(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
    UI::UINodeId button)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::buttonPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto paint = updater.buttonPaint(button);
    if (!paint)
    {
        return Core::failure(rememberFirstError(std::move(paint.error()), Operation));
    }
    return *paint;
}

Core::Status PrimaryWindowUICapabilityState::setText(u64 epoch, PrimaryWindowUIPhase phase,
                                                     UI::UITreeUpdater& updater, UI::UINodeId node,
                                                     std::string_view utf8)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setText";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setText(node, utf8);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setTextStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                          UI::UITreeUpdater& updater, UI::UINodeId node,
                                                          const UI::UITextStyle& style)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setTextStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setTextStyle(node, style);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<std::string_view> PrimaryWindowUICapabilityState::text(u64 epoch, PrimaryWindowUIPhase phase,
                                                                    UI::UITreeUpdater& updater, UI::UINodeId node)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::text";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(status.error());
    }
    auto result = updater.text(node);
    if (!result)
    {
        return Core::failure(rememberFirstError(std::move(result.error()), Operation));
    }
    return *result;
}

Core::Result<UI::UITextStyle> PrimaryWindowUICapabilityState::textStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                                        UI::UITreeUpdater& updater, UI::UINodeId node)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::textStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(status.error());
    }
    auto result = updater.textStyle(node);
    if (!result)
    {
        return Core::failure(rememberFirstError(std::move(result.error()), Operation));
    }
    return *result;
}

Core::Status PrimaryWindowUICapabilityState::setTextSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                                               UI::UITreeUpdater& updater,
                                                               UI::UINodeId textEdit,
                                                               UI::UITextSelection selection)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setTextSelection";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setTextSelection(textEdit, selection);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UITextSelection>
PrimaryWindowUICapabilityState::textSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                               const UI::UITreeUpdater& updater,
                                               UI::UINodeId textEdit)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::textSelection";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(status.error());
    }
    auto result = updater.textSelection(textEdit);
    if (!result)
    {
        return Core::failure(rememberFirstError(std::move(result.error()), Operation));
    }
    return *result;
}

Core::Status PrimaryWindowUICapabilityState::setButtonAction(u64 epoch, PrimaryWindowUIPhase phase,
                                                             UI::UITreeUpdater& updater, UI::UINodeId button,
                                                             UI::UIButtonActionCallback callback)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setButtonAction";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setButtonAction(button, std::move(callback));
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearButtonAction(u64 epoch, PrimaryWindowUIPhase phase,
                                                               UI::UITreeUpdater& updater, UI::UINodeId button)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearButtonAction";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearButtonAction(button);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<bool> PrimaryWindowUICapabilityState::isButtonPressed(u64 epoch, PrimaryWindowUIPhase phase,
                                                                   const UI::UITreeUpdater& updater,
                                                                   UI::UINodeId button)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isButtonPressed";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto pressed = updater.isButtonPressed(button);
    if (!pressed)
    {
        return Core::failure(rememberFirstError(std::move(pressed.error()), Operation));
    }
    return *pressed;
}

Core::Status PrimaryWindowUICapabilityState::setCheckboxAction(u64 epoch, PrimaryWindowUIPhase phase,
                                                               UI::UITreeUpdater& updater, UI::UINodeId checkbox,
                                                               UI::UIButtonActionCallback callback)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setCheckboxAction";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setCheckboxAction(checkbox, std::move(callback));
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearCheckboxAction(u64 epoch, PrimaryWindowUIPhase phase,
                                                                 UI::UITreeUpdater& updater, UI::UINodeId checkbox)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearCheckboxAction";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearCheckboxAction(checkbox);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setCheckboxPaint(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId checkbox, const UI::UICheckboxPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setCheckboxPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setCheckboxPaint(checkbox, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UICheckboxPaint> PrimaryWindowUICapabilityState::checkboxPaint(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
    UI::UINodeId checkbox)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::checkboxPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto paint = updater.checkboxPaint(checkbox);
    if (!paint)
    {
        return Core::failure(rememberFirstError(std::move(paint.error()), Operation));
    }
    return *paint;
}

Core::Status PrimaryWindowUICapabilityState::setChecked(u64 epoch, PrimaryWindowUIPhase phase,
                                                        UI::UITreeUpdater& updater, UI::UINodeId checkbox, bool checked)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setChecked";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setChecked(checkbox, checked);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<bool> PrimaryWindowUICapabilityState::isChecked(u64 epoch, PrimaryWindowUIPhase phase,
                                                             const UI::UITreeUpdater& updater, UI::UINodeId checkbox)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isChecked";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto checked = updater.isChecked(checkbox);
    if (!checked)
    {
        return Core::failure(rememberFirstError(std::move(checked.error()), Operation));
    }
    return *checked;
}

Core::Result<bool> PrimaryWindowUICapabilityState::isCheckboxPressed(u64 epoch, PrimaryWindowUIPhase phase,
                                                                     const UI::UITreeUpdater& updater,
                                                                     UI::UINodeId checkbox)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isCheckboxPressed";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto pressed = updater.isCheckboxPressed(checkbox);
    if (!pressed)
    {
        return Core::failure(rememberFirstError(std::move(pressed.error()), Operation));
    }
    return *pressed;
}

Core::Status PrimaryWindowUICapabilityState::setSliderRange(u64 epoch, PrimaryWindowUIPhase phase,
                                                            UI::UITreeUpdater& updater, UI::UINodeId slider,
                                                            float minValue, float maxValue, float step)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setSliderRange";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setSliderRange(slider, minValue, maxValue, step);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setSliderValue(u64 epoch, PrimaryWindowUIPhase phase,
                                                            UI::UITreeUpdater& updater, UI::UINodeId slider,
                                                            float value)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setSliderValue";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setSliderValue(slider, value);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<float> PrimaryWindowUICapabilityState::sliderValue(u64 epoch, PrimaryWindowUIPhase phase,
                                                                const UI::UITreeUpdater& updater, UI::UINodeId slider)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::sliderValue";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto value = updater.sliderValue(slider);
    if (!value)
    {
        return Core::failure(rememberFirstError(std::move(value.error()), Operation));
    }
    return *value;
}

Core::Status PrimaryWindowUICapabilityState::setSliderPaint(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId slider, const UI::UISliderPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setSliderPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setSliderPaint(slider, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UISliderPaint> PrimaryWindowUICapabilityState::sliderPaint(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
    UI::UINodeId slider)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::sliderPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto paint = updater.sliderPaint(slider);
    if (!paint)
    {
        return Core::failure(rememberFirstError(std::move(paint.error()), Operation));
    }
    return *paint;
}

Core::Status PrimaryWindowUICapabilityState::setSliderChangeCallback(u64 epoch, PrimaryWindowUIPhase phase,
                                                                     UI::UITreeUpdater& updater, UI::UINodeId slider,
                                                                     UI::UISliderChangeCallback callback)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setSliderChangeCallback";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setSliderChangeCallback(slider, std::move(callback));
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearSliderChangeCallback(u64 epoch, PrimaryWindowUIPhase phase,
                                                                       UI::UITreeUpdater& updater, UI::UINodeId slider)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearSliderChangeCallback";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearSliderChangeCallback(slider);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<bool> PrimaryWindowUICapabilityState::isSliderDragging(u64 epoch, PrimaryWindowUIPhase phase,
                                                                    const UI::UITreeUpdater& updater,
                                                                    UI::UINodeId slider)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isSliderDragging";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto dragging = updater.isSliderDragging(slider);
    if (!dragging)
    {
        return Core::failure(rememberFirstError(std::move(dragging.error()), Operation));
    }
    return *dragging;
}

Core::Status PrimaryWindowUICapabilityState::setProgressBarRange(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId progressBar, float minValue, float maxValue)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setProgressBarRange";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setProgressBarRange(progressBar, minValue, maxValue);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setProgressBarValue(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId progressBar, float value)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setProgressBarValue";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setProgressBarValue(progressBar, value);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<float> PrimaryWindowUICapabilityState::progressBarValue(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
    UI::UINodeId progressBar)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::progressBarValue";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto value = updater.progressBarValue(progressBar);
    if (!value)
    {
        return Core::failure(rememberFirstError(std::move(value.error()), Operation));
    }
    return *value;
}

Core::Status PrimaryWindowUICapabilityState::setProgressBarPaint(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId progressBar, const UI::UIProgressBarPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setProgressBarPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setProgressBarPaint(progressBar, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIProgressBarPaint> PrimaryWindowUICapabilityState::progressBarPaint(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
    UI::UINodeId progressBar)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::progressBarPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto paint = updater.progressBarPaint(progressBar);
    if (!paint)
    {
        return Core::failure(rememberFirstError(std::move(paint.error()), Operation));
    }
    return *paint;
}

Core::Status PrimaryWindowUICapabilityState::setRadioButtonPaint(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId radioButton, const UI::UIRadioButtonPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setRadioButtonPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setRadioButtonPaint(radioButton, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIRadioButtonPaint> PrimaryWindowUICapabilityState::radioButtonPaint(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
    UI::UINodeId radioButton)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::radioButtonPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto paint = updater.radioButtonPaint(radioButton);
    if (!paint)
    {
        return Core::failure(rememberFirstError(std::move(paint.error()), Operation));
    }
    return *paint;
}

Core::Status PrimaryWindowUICapabilityState::setRadioButtonAction(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId radioButton, UI::UIButtonActionCallback callback)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setRadioButtonAction";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setRadioButtonAction(radioButton, std::move(callback));
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearRadioButtonAction(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId radioButton)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearRadioButtonAction";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearRadioButtonAction(radioButton);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setRadioButtonSelected(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
    UI::UINodeId radioButton, bool selected)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setRadioButtonSelected";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setRadioButtonSelected(radioButton, selected);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<bool> PrimaryWindowUICapabilityState::isRadioButtonSelected(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
    UI::UINodeId radioButton)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isRadioButtonSelected";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto selected = updater.isRadioButtonSelected(radioButton);
    if (!selected)
    {
        return Core::failure(rememberFirstError(std::move(selected.error()), Operation));
    }
    return *selected;
}

Core::Result<bool> PrimaryWindowUICapabilityState::isRadioButtonPressed(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
    UI::UINodeId radioButton)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isRadioButtonPressed";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto pressed = updater.isRadioButtonPressed(radioButton);
    if (!pressed)
    {
        return Core::failure(rememberFirstError(std::move(pressed.error()), Operation));
    }
    return *pressed;
}

Core::Result<UI::UIRoutedPointerListenerToken> PrimaryWindowUICapabilityState::addRoutedPointerListener(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater, UI::UIRoutedPointerListenerDesc descriptor,
    UI::UIRoutedPointerCallback callback)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::addRoutedPointerListener";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto listener = updater.addRoutedPointerListener(descriptor, std::move(callback));
    if (!listener)
    {
        return Core::failure(rememberFirstError(std::move(listener.error()), Operation));
    }
    return std::move(*listener);
}

Core::Status PrimaryWindowUICapabilityState::destroy(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                     UI::UINodeId node)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::destroy";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.destroy(node);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

} // namespace Tina::Runtime::Detail
