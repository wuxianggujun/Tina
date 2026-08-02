#include "PrimaryWindowUICapabilityState.hpp"

#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIErrors.hpp>

#include <limits>
#include <exception>
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

PrimaryWindowUICapabilityState::PrimaryWindowUICapabilityState(usize imageResolverCapacity)
    : ownerThreadId_(std::this_thread::get_id()), imageResolverSlots_(imageResolverCapacity)
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

    if (buildTransaction_.has_value())
    {
        buildTransaction_.reset();
        buildTransactionEpoch_ = 0;
        buildTransactionPhase_ = PrimaryWindowUIPhase::None;
        static_cast<void>(rememberFirstError(
            Core::Error{UI::UIErrorCode::BuildTransactionInProgress,
                        "A primary-window UI build transaction escaped its Runtime phase"},
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

    buildTransaction_.reset();
    buildTransactionEpoch_ = 0;
    buildTransactionPhase_ = PrimaryWindowUIPhase::None;
    firstError_.reset();
    context_ = nullptr;
    phase_ = PrimaryWindowUIPhase::None;
}

bool PrimaryWindowUICapabilityState::hasPrimaryWindowUI(u64 epoch, PrimaryWindowUIPhase phase) const noexcept
{
    return std::this_thread::get_id() == ownerThreadId_ && epoch != 0 && epoch == epoch_ &&
           phase != PrimaryWindowUIPhase::None && phase == phase_ && context_ != nullptr;
}

Core::Result<UI::UICommittedSemanticsView>
PrimaryWindowUICapabilityState::committedSemantics(u64 epoch, PrimaryWindowUIPhase phase)
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

Core::Result<PrimaryWindowUIImageResolverRegistration>
PrimaryWindowUICapabilityState::bindImageResolver(
    u64 epoch, UI::UIRootOwner& rootOwner,
    Render::Texture2DFrameResourceResolver resolver)
{
    constexpr std::string_view Operation = "PrimaryWindowUIRootBuilder::bindImageResolver";
    if (Core::Status status = validate(epoch, PrimaryWindowUIPhase::GameStateEnter, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (!resolver.hasValue())
    {
        return Core::failure(rememberFirstError(
            Core::Error{Core::CoreErrorCode::InvalidArgument,
                        "A primary-window UI image resolver requires a callback"},
            Operation));
    }

    auto updater = context_->treeUpdater(rootOwner);
    if (!updater)
    {
        return Core::failure(rememberFirstError(std::move(updater.error()), Operation));
    }
    const UI::UINodeId root = rootOwner.rootNodeId();
    for (const ImageResolverSlot& slot : imageResolverSlots_)
    {
        if (slot.active && slot.root == root)
        {
            return Core::failure(rememberFirstError(
                Core::Error{Core::CoreErrorCode::InvalidArgument,
                            "The primary-window UI root already has an image resolver"},
                Operation));
        }
    }
    for (usize index = 0; index < imageResolverSlots_.size(); ++index)
    {
        ImageResolverSlot& slot = imageResolverSlots_[index];
        if (!slot.active && !slot.retired)
        {
            slot.root = root;
            slot.resolver = resolver;
            slot.active = true;
            return PrimaryWindowUIImageResolverRegistration{
                *this, static_cast<u32>(index), slot.generation};
        }
    }
    return Core::failure(rememberFirstError(
        Core::Error{Core::CoreErrorCode::CapacityExceeded,
                    "Primary-window UI image resolver capacity has been exhausted"},
        Operation));
}

const Render::Texture2DFrameResourceResolver*
PrimaryWindowUICapabilityState::findImageResolver(UI::UINodeId root) const noexcept
{
    if (std::this_thread::get_id() != ownerThreadId_ || !root.hasValue())
    {
        return nullptr;
    }
    for (const ImageResolverSlot& slot : imageResolverSlots_)
    {
        if (slot.active && slot.root == root)
        {
            return &slot.resolver;
        }
    }
    return nullptr;
}

void PrimaryWindowUICapabilityState::unbindImageResolver(u32 slotIndex, u32 generation) noexcept
{
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        std::terminate();
    }
    if (slotIndex >= imageResolverSlots_.size())
    {
        return;
    }
    ImageResolverSlot& slot = imageResolverSlots_[slotIndex];
    if (!slot.active || generation == 0 || slot.generation != generation)
    {
        return;
    }
    slot.root = {};
    slot.resolver = {};
    slot.active = false;
    if (slot.generation == (std::numeric_limits<u32>::max)())
    {
        slot.retired = true;
        return;
    }
    ++slot.generation;
}

bool PrimaryWindowUICapabilityState::isImageResolverActive(u32 slotIndex, u32 generation) const noexcept
{
    if (std::this_thread::get_id() != ownerThreadId_ || slotIndex >= imageResolverSlots_.size())
    {
        return false;
    }
    const ImageResolverSlot& slot = imageResolverSlots_[slotIndex];
    return slot.active && generation != 0 && slot.generation == generation;
}

Core::Result<UI::UIStyleClassId>
PrimaryWindowUICapabilityState::registerStyleClass(u64 epoch)
{
    constexpr std::string_view Operation =
        "PrimaryWindowUIRootBuilder::registerStyleClass";
    if (Core::Status status =
            validate(epoch, PrimaryWindowUIPhase::GameStateEnter, true, Operation);
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto styleClass = context_->registerStyleClass();
    if (!styleClass)
    {
        return Core::failure(
            rememberFirstError(std::move(styleClass.error()), Operation));
    }
    return *styleClass;
}

Core::Result<UI::UIStyleTokenId>
PrimaryWindowUICapabilityState::registerStyleColorToken(
    u64 epoch, UI::UIStraightSrgba8Color value)
{
    constexpr std::string_view Operation =
        "PrimaryWindowUIRootBuilder::registerStyleColorToken";
    if (Core::Status status =
            validate(epoch, PrimaryWindowUIPhase::GameStateEnter, true, Operation);
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto token = context_->registerStyleColorToken(value);
    if (!token)
    {
        return Core::failure(rememberFirstError(std::move(token.error()), Operation));
    }
    return *token;
}

Core::Status PrimaryWindowUICapabilityState::installStyleSheet(
    u64 epoch, std::span<const UI::UIStyleBoxFillRule> rules)
{
    constexpr std::string_view Operation =
        "PrimaryWindowUIRootBuilder::installStyleSheet";
    if (Core::Status status =
            validate(epoch, PrimaryWindowUIPhase::GameStateEnter, true, Operation);
        !status)
    {
        return status;
    }
    Core::Status status = context_->installStyleSheet(rules);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
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

Core::Result<UI::UINodeId>
PrimaryWindowUICapabilityState::createElement(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId parent,
                                              const UI::UIElementDescriptor& descriptor)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::createElement";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto child = updater.createElement(parent, descriptor);
    if (!child)
    {
        return Core::failure(rememberFirstError(std::move(child.error()), Operation));
    }
    return *child;
}

Core::Result<PrimaryWindowUIBuildTransaction>
PrimaryWindowUICapabilityState::beginBuildTransaction(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater, UI::UINodeId parent,
    const UI::UIElementDescriptor& rootDescriptor, UI::UIComponentBuildBudget budget)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::beginBuildTransaction";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (buildTransaction_.has_value())
    {
        return Core::failure(rememberFirstError(
            Core::Error{UI::UIErrorCode::BuildTransactionInProgress,
                        "Only one primary-window UI build transaction may be active per Runtime phase"},
            Operation));
    }

    auto transaction = updater.beginBuildTransaction(parent, rootDescriptor, budget);
    if (!transaction)
    {
        return Core::failure(rememberFirstError(std::move(transaction.error()), Operation));
    }
    buildTransaction_.emplace(std::move(*transaction));
    buildTransactionEpoch_ = epoch;
    buildTransactionPhase_ = phase;
    return PrimaryWindowUIBuildTransaction{*this, epoch, phase};
}

Core::Result<UI::UINodeId>
PrimaryWindowUICapabilityState::createElementFromBuildTransaction(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UINodeId parent,
    const UI::UIElementDescriptor& descriptor)
{
    constexpr std::string_view Operation = "PrimaryWindowUIBuildTransaction::createElement";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (!isBuildTransactionActive(epoch, phase))
    {
        return Core::failure(rememberFirstError(
            Core::Error{UI::UIErrorCode::InvalidNode,
                        "The primary-window UI build transaction is not active"},
            Operation));
    }

    auto child = buildTransaction_->createElement(parent, descriptor);
    if (!child)
    {
        Core::Error error = rememberFirstError(std::move(child.error()), Operation);
        buildTransaction_.reset();
        buildTransactionEpoch_ = 0;
        buildTransactionPhase_ = PrimaryWindowUIPhase::None;
        return Core::failure(std::move(error));
    }
    return *child;
}

Core::Result<UI::UINodeId>
PrimaryWindowUICapabilityState::commitBuildTransaction(u64 epoch, PrimaryWindowUIPhase phase)
{
    constexpr std::string_view Operation = "PrimaryWindowUIBuildTransaction::commit";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (!isBuildTransactionActive(epoch, phase))
    {
        return Core::failure(rememberFirstError(
            Core::Error{UI::UIErrorCode::InvalidNode,
                        "The primary-window UI build transaction is not active"},
            Operation));
    }

    auto componentRoot = buildTransaction_->commit();
    if (!componentRoot)
    {
        Core::Error error = rememberFirstError(std::move(componentRoot.error()), Operation);
        buildTransaction_.reset();
        buildTransactionEpoch_ = 0;
        buildTransactionPhase_ = PrimaryWindowUIPhase::None;
        return Core::failure(std::move(error));
    }
    buildTransaction_.reset();
    buildTransactionEpoch_ = 0;
    buildTransactionPhase_ = PrimaryWindowUIPhase::None;
    return *componentRoot;
}

void PrimaryWindowUICapabilityState::resetBuildTransaction(u64 epoch,
                                                            PrimaryWindowUIPhase phase) noexcept
{
    if (std::this_thread::get_id() != ownerThreadId_ || epoch == 0 || epoch != epoch_ ||
        epoch != buildTransactionEpoch_ || phase == PrimaryWindowUIPhase::None ||
        phase != phase_ || phase != buildTransactionPhase_ || !buildTransaction_.has_value())
    {
        return;
    }
    buildTransaction_.reset();
    buildTransactionEpoch_ = 0;
    buildTransactionPhase_ = PrimaryWindowUIPhase::None;
}

UI::UINodeId PrimaryWindowUICapabilityState::buildTransactionRootNodeId(
    u64 epoch, PrimaryWindowUIPhase phase) const noexcept
{
    return isBuildTransactionActive(epoch, phase) ? buildTransaction_->rootNodeId() : UI::UINodeId{};
}

UI::UIComponentBuildBudget PrimaryWindowUICapabilityState::buildTransactionRemainingBudget(
    u64 epoch, PrimaryWindowUIPhase phase) const noexcept
{
    return isBuildTransactionActive(epoch, phase) ? buildTransaction_->remainingBudget()
                                                   : UI::UIComponentBuildBudget{};
}

bool PrimaryWindowUICapabilityState::isBuildTransactionActive(
    u64 epoch, PrimaryWindowUIPhase phase) const noexcept
{
    return std::this_thread::get_id() == ownerThreadId_ && epoch != 0 && epoch == epoch_ &&
           epoch == buildTransactionEpoch_ && phase != PrimaryWindowUIPhase::None && phase == phase_ &&
           phase == buildTransactionPhase_ && buildTransaction_.has_value() && buildTransaction_->isActive();
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

Core::Status PrimaryWindowUICapabilityState::setEnabled(u64 epoch, PrimaryWindowUIPhase phase,
                                                        UI::UITreeUpdater& updater, UI::UINodeId node, bool enabled)
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

Core::Result<bool> PrimaryWindowUICapabilityState::isEnabled(u64 epoch, PrimaryWindowUIPhase phase,
                                                             const UI::UITreeUpdater& updater, UI::UINodeId node)
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

Core::Status PrimaryWindowUICapabilityState::setFocusScopeMode(u64 epoch, PrimaryWindowUIPhase phase,
                                                               UI::UITreeUpdater& updater, UI::UINodeId node,
                                                               UI::UIFocusScopeMode mode)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setFocusScopeMode";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setFocusScopeMode(node, mode);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIFocusScopeMode> PrimaryWindowUICapabilityState::focusScopeMode(u64 epoch, PrimaryWindowUIPhase phase,
                                                                                  const UI::UITreeUpdater& updater,
                                                                                  UI::UINodeId node)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::focusScopeMode";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto mode = updater.focusScopeMode(node);
    if (!mode)
    {
        return Core::failure(rememberFirstError(std::move(mode.error()), Operation));
    }
    return *mode;
}

Core::Status PrimaryWindowUICapabilityState::requestFocus(u64 epoch, PrimaryWindowUIPhase phase,
                                                          UI::UITreeUpdater& updater, UI::UINodeId node)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::requestFocus";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.requestFocus(node);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearFocus(u64 epoch, PrimaryWindowUIPhase phase,
                                                        UI::UITreeUpdater& updater)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearFocus";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearFocus();
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setStyleRole(u64 epoch, PrimaryWindowUIPhase phase,
                                                          UI::UITreeUpdater& updater, UI::UINodeId node,
                                                          UI::UIStyleRoleId role)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setStyleRole";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setStyleRole(node, role);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIStyleRoleId>
PrimaryWindowUICapabilityState::styleRole(u64 epoch, PrimaryWindowUIPhase phase,
                                          const UI::UITreeUpdater& updater, UI::UINodeId node)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::styleRole";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto role = updater.styleRole(node);
    if (!role)
    {
        return Core::failure(rememberFirstError(std::move(role.error()), Operation));
    }
    return *role;
}

Core::Result<UI::UIStraightSrgba8Color>
PrimaryWindowUICapabilityState::styleColorToken(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UIStyleTokenId token)
{
    constexpr std::string_view Operation =
        "PrimaryWindowUITreeUpdater::styleColorToken";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto value = context_->styleColorToken(token);
    if (!value)
    {
        return Core::failure(rememberFirstError(std::move(value.error()), Operation));
    }
    return *value;
}

Core::Status PrimaryWindowUICapabilityState::setStyleColorToken(
    u64 epoch, PrimaryWindowUIPhase phase, UI::UIStyleTokenId token,
    UI::UIStraightSrgba8Color value)
{
    constexpr std::string_view Operation =
        "PrimaryWindowUITreeUpdater::setStyleColorToken";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = context_->setStyleColorToken(token, value);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearOverride(u64 epoch, PrimaryWindowUIPhase phase,
                                                           UI::UITreeUpdater& updater, UI::UINodeId node,
                                                           UI::UIStyleOverride properties)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearOverride";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearOverride(node, properties);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UITheme> PrimaryWindowUICapabilityState::productTheme(u64 epoch, PrimaryWindowUIPhase phase)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::productTheme";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return context_->productTheme();
}

Core::Status PrimaryWindowUICapabilityState::setProductTheme(u64 epoch, PrimaryWindowUIPhase phase,
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

Core::Status PrimaryWindowUICapabilityState::setButtonPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                            UI::UITreeUpdater& updater, UI::UINodeId button,
                                                            const UI::UIButtonPaint& paint)
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

Core::Result<UI::UIButtonPaint> PrimaryWindowUICapabilityState::buttonPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                                            const UI::UITreeUpdater& updater,
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

Core::Status PrimaryWindowUICapabilityState::setText(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                     UI::UINodeId node, std::string_view utf8)
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

Core::Status PrimaryWindowUICapabilityState::setContentAlignment(u64 epoch, PrimaryWindowUIPhase phase,
                                                                  UI::UITreeUpdater& updater, UI::UINodeId node,
                                                                  UI::UIContentAlignment alignment)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setContentAlignment";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setContentAlignment(node, alignment);
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

Core::Result<UI::UIContentAlignment>
PrimaryWindowUICapabilityState::contentAlignment(u64 epoch, PrimaryWindowUIPhase phase,
                                                  const UI::UITreeUpdater& updater, UI::UINodeId node)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::contentAlignment";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(status.error());
    }
    auto result = updater.contentAlignment(node);
    if (!result)
    {
        return Core::failure(rememberFirstError(std::move(result.error()), Operation));
    }
    return *result;
}

Core::Status PrimaryWindowUICapabilityState::setTextSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                                              UI::UITreeUpdater& updater, UI::UINodeId textEdit,
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

Core::Result<UI::UITextSelection> PrimaryWindowUICapabilityState::textSelection(u64 epoch, PrimaryWindowUIPhase phase,
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

Core::Status PrimaryWindowUICapabilityState::setTextEditPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                              UI::UITreeUpdater& updater, UI::UINodeId textEdit,
                                                              const UI::UITextEditPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setTextEditPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setTextEditPaint(textEdit, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UITextEditPaint>
PrimaryWindowUICapabilityState::textEditPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                              const UI::UITreeUpdater& updater, UI::UINodeId textEdit)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::textEditPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(status.error());
    }
    auto result = updater.textEditPaint(textEdit);
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

Core::Status PrimaryWindowUICapabilityState::setCheckboxPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                              UI::UITreeUpdater& updater, UI::UINodeId checkbox,
                                                              const UI::UICheckboxPaint& paint)
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

Core::Result<UI::UICheckboxPaint> PrimaryWindowUICapabilityState::checkboxPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                                                const UI::UITreeUpdater& updater,
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

Core::Status PrimaryWindowUICapabilityState::setSliderPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                            UI::UITreeUpdater& updater, UI::UINodeId slider,
                                                            const UI::UISliderPaint& paint)
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

Core::Result<UI::UISliderPaint> PrimaryWindowUICapabilityState::sliderPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                                            const UI::UITreeUpdater& updater,
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

Core::Status PrimaryWindowUICapabilityState::setScrollViewStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                                UI::UITreeUpdater& updater,
                                                                UI::UINodeId scrollView,
                                                                const UI::UIScrollViewStyle& style)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setScrollViewStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setScrollViewStyle(scrollView, style);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIScrollViewStyle>
PrimaryWindowUICapabilityState::scrollViewStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                const UI::UITreeUpdater& updater, UI::UINodeId scrollView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::scrollViewStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto style = updater.scrollViewStyle(scrollView);
    if (!style)
    {
        return Core::failure(rememberFirstError(std::move(style.error()), Operation));
    }
    return *style;
}

Core::Status PrimaryWindowUICapabilityState::setScrollViewOffset(u64 epoch, PrimaryWindowUIPhase phase,
                                                                 UI::UITreeUpdater& updater,
                                                                 UI::UINodeId scrollView,
                                                                 UI::UIScrollOffset offset)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setScrollViewOffset";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setScrollViewOffset(scrollView, offset);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIScrollOffset>
PrimaryWindowUICapabilityState::scrollViewOffset(u64 epoch, PrimaryWindowUIPhase phase,
                                                 const UI::UITreeUpdater& updater, UI::UINodeId scrollView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::scrollViewOffset";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto offset = updater.scrollViewOffset(scrollView);
    if (!offset)
    {
        return Core::failure(rememberFirstError(std::move(offset.error()), Operation));
    }
    return *offset;
}

Core::Result<UI::UIScrollViewMetrics>
PrimaryWindowUICapabilityState::scrollViewMetrics(u64 epoch, PrimaryWindowUIPhase phase,
                                                  const UI::UITreeUpdater& updater, UI::UINodeId scrollView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::scrollViewMetrics";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto metrics = updater.scrollViewMetrics(scrollView);
    if (!metrics)
    {
        return Core::failure(rememberFirstError(std::move(metrics.error()), Operation));
    }
    return *metrics;
}

Core::Status PrimaryWindowUICapabilityState::setScrollViewPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                                UI::UITreeUpdater& updater,
                                                                UI::UINodeId scrollView,
                                                                const UI::UIScrollViewPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setScrollViewPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setScrollViewPaint(scrollView, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIScrollViewPaint>
PrimaryWindowUICapabilityState::scrollViewPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                const UI::UITreeUpdater& updater, UI::UINodeId scrollView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::scrollViewPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto paint = updater.scrollViewPaint(scrollView);
    if (!paint)
    {
        return Core::failure(rememberFirstError(std::move(paint.error()), Operation));
    }
    return *paint;
}

Core::Result<bool> PrimaryWindowUICapabilityState::isScrollViewDragging(u64 epoch, PrimaryWindowUIPhase phase,
                                                                       const UI::UITreeUpdater& updater,
                                                                       UI::UINodeId scrollView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isScrollViewDragging";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto dragging = updater.isScrollViewDragging(scrollView);
    if (!dragging)
    {
        return Core::failure(rememberFirstError(std::move(dragging.error()), Operation));
    }
    return *dragging;
}

Core::Status PrimaryWindowUICapabilityState::setListViewDataSource(u64 epoch, PrimaryWindowUIPhase phase,
                                                                   UI::UITreeUpdater& updater,
                                                                   UI::UINodeId listView,
                                                                   UI::UIListViewDataSource source)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setListViewDataSource";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setListViewDataSource(listView, source);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearListViewDataSource(u64 epoch, PrimaryWindowUIPhase phase,
                                                                     UI::UITreeUpdater& updater,
                                                                     UI::UINodeId listView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearListViewDataSource";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearListViewDataSource(listView);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::invalidateListViewItems(u64 epoch, PrimaryWindowUIPhase phase,
                                                                     UI::UITreeUpdater& updater,
                                                                     UI::UINodeId listView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::invalidateListViewItems";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.invalidateListViewItems(listView);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setListViewStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                              UI::UITreeUpdater& updater, UI::UINodeId listView,
                                                              const UI::UIListViewStyle& style)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setListViewStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setListViewStyle(listView, style);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIListViewStyle>
PrimaryWindowUICapabilityState::listViewStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                              const UI::UITreeUpdater& updater, UI::UINodeId listView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::listViewStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto style = updater.listViewStyle(listView);
    if (!style)
    {
        return Core::failure(rememberFirstError(std::move(style.error()), Operation));
    }
    return *style;
}

Core::Status PrimaryWindowUICapabilityState::setListViewPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                              UI::UITreeUpdater& updater, UI::UINodeId listView,
                                                              const UI::UIListViewPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setListViewPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setListViewPaint(listView, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIListViewPaint>
PrimaryWindowUICapabilityState::listViewPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                              const UI::UITreeUpdater& updater, UI::UINodeId listView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::listViewPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto paint = updater.listViewPaint(listView);
    if (!paint)
    {
        return Core::failure(rememberFirstError(std::move(paint.error()), Operation));
    }
    return *paint;
}

Core::Result<UI::UIListViewMetrics>
PrimaryWindowUICapabilityState::listViewMetrics(u64 epoch, PrimaryWindowUIPhase phase,
                                                const UI::UITreeUpdater& updater, UI::UINodeId listView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::listViewMetrics";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto metrics = updater.listViewMetrics(listView);
    if (!metrics)
    {
        return Core::failure(rememberFirstError(std::move(metrics.error()), Operation));
    }
    return *metrics;
}

Core::Status PrimaryWindowUICapabilityState::setListViewSelectedIndex(u64 epoch, PrimaryWindowUIPhase phase,
                                                                      UI::UITreeUpdater& updater,
                                                                      UI::UINodeId listView, u64 logicalIndex)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setListViewSelectedIndex";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setListViewSelectedIndex(listView, logicalIndex);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearListViewSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                                                    UI::UITreeUpdater& updater,
                                                                    UI::UINodeId listView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearListViewSelection";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearListViewSelection(listView);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIListViewSelection>
PrimaryWindowUICapabilityState::listViewSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                                  const UI::UITreeUpdater& updater, UI::UINodeId listView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::listViewSelection";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto selection = updater.listViewSelection(listView);
    if (!selection)
    {
        return Core::failure(rememberFirstError(std::move(selection.error()), Operation));
    }
    return *selection;
}

Core::Status PrimaryWindowUICapabilityState::scrollListViewToIndex(u64 epoch, PrimaryWindowUIPhase phase,
                                                                   UI::UITreeUpdater& updater,
                                                                   UI::UINodeId listView, u64 logicalIndex,
                                                                   UI::UIListViewScrollAlignment alignment)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::scrollListViewToIndex";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.scrollListViewToIndex(listView, logicalIndex, alignment);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setTreeViewDataSource(u64 epoch, PrimaryWindowUIPhase phase,
                                                                   UI::UITreeUpdater& updater,
                                                                   UI::UINodeId treeView,
                                                                   UI::UITreeViewDataSource source)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setTreeViewDataSource";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setTreeViewDataSource(treeView, source);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearTreeViewDataSource(u64 epoch, PrimaryWindowUIPhase phase,
                                                                     UI::UITreeUpdater& updater,
                                                                     UI::UINodeId treeView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearTreeViewDataSource";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearTreeViewDataSource(treeView);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::invalidateTreeViewItems(u64 epoch, PrimaryWindowUIPhase phase,
                                                                     UI::UITreeUpdater& updater,
                                                                     UI::UINodeId treeView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::invalidateTreeViewItems";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.invalidateTreeViewItems(treeView);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setTreeViewStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                              UI::UITreeUpdater& updater, UI::UINodeId treeView,
                                                              const UI::UITreeViewStyle& style)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setTreeViewStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setTreeViewStyle(treeView, style);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UITreeViewStyle>
PrimaryWindowUICapabilityState::treeViewStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                              const UI::UITreeUpdater& updater, UI::UINodeId treeView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::treeViewStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto style = updater.treeViewStyle(treeView);
    if (!style)
    {
        return Core::failure(rememberFirstError(std::move(style.error()), Operation));
    }
    return *style;
}

Core::Status PrimaryWindowUICapabilityState::setTreeViewPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                              UI::UITreeUpdater& updater, UI::UINodeId treeView,
                                                              const UI::UITreeViewPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setTreeViewPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setTreeViewPaint(treeView, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UITreeViewPaint>
PrimaryWindowUICapabilityState::treeViewPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                              const UI::UITreeUpdater& updater, UI::UINodeId treeView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::treeViewPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto paint = updater.treeViewPaint(treeView);
    if (!paint)
    {
        return Core::failure(rememberFirstError(std::move(paint.error()), Operation));
    }
    return *paint;
}

Core::Result<UI::UITreeViewMetrics>
PrimaryWindowUICapabilityState::treeViewMetrics(u64 epoch, PrimaryWindowUIPhase phase,
                                                const UI::UITreeUpdater& updater, UI::UINodeId treeView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::treeViewMetrics";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto metrics = updater.treeViewMetrics(treeView);
    if (!metrics)
    {
        return Core::failure(rememberFirstError(std::move(metrics.error()), Operation));
    }
    return *metrics;
}

Core::Status PrimaryWindowUICapabilityState::setTreeViewSelectedIndex(u64 epoch, PrimaryWindowUIPhase phase,
                                                                      UI::UITreeUpdater& updater,
                                                                      UI::UINodeId treeView, u64 logicalIndex)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setTreeViewSelectedIndex";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setTreeViewSelectedIndex(treeView, logicalIndex);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::clearTreeViewSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                                                    UI::UITreeUpdater& updater,
                                                                    UI::UINodeId treeView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::clearTreeViewSelection";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.clearTreeViewSelection(treeView);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UITreeViewSelection>
PrimaryWindowUICapabilityState::treeViewSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                                  const UI::UITreeUpdater& updater, UI::UINodeId treeView)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::treeViewSelection";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto selection = updater.treeViewSelection(treeView);
    if (!selection)
    {
        return Core::failure(rememberFirstError(std::move(selection.error()), Operation));
    }
    return *selection;
}

Core::Status PrimaryWindowUICapabilityState::setTreeViewItemExpanded(u64 epoch, PrimaryWindowUIPhase phase,
                                                                     UI::UITreeUpdater& updater,
                                                                     UI::UINodeId treeView, u64 logicalIndex,
                                                                     bool expanded)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setTreeViewItemExpanded";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setTreeViewItemExpanded(treeView, logicalIndex, expanded);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::scrollTreeViewToIndex(u64 epoch, PrimaryWindowUIPhase phase,
                                                                   UI::UITreeUpdater& updater,
                                                                   UI::UINodeId treeView, u64 logicalIndex,
                                                                   UI::UITreeViewScrollAlignment alignment)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::scrollTreeViewToIndex";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.scrollTreeViewToIndex(treeView, logicalIndex, alignment);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Status PrimaryWindowUICapabilityState::setPopupStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                           UI::UITreeUpdater& updater, UI::UINodeId popup,
                                                           const UI::UIPopupStyle& style)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setPopupStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setPopupStyle(popup, style);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIPopupStyle> PrimaryWindowUICapabilityState::popupStyle(u64 epoch,
                                                                         PrimaryWindowUIPhase phase,
                                                                         const UI::UITreeUpdater& updater,
                                                                         UI::UINodeId popup)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::popupStyle";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto style = updater.popupStyle(popup);
    if (!style)
    {
        return Core::failure(rememberFirstError(std::move(style.error()), Operation));
    }
    return *style;
}

Core::Status PrimaryWindowUICapabilityState::setPopupOpen(u64 epoch, PrimaryWindowUIPhase phase,
                                                          UI::UITreeUpdater& updater, UI::UINodeId popup, bool open)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setPopupOpen";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setPopupOpen(popup, open);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<bool> PrimaryWindowUICapabilityState::isPopupOpen(u64 epoch, PrimaryWindowUIPhase phase,
                                                              const UI::UITreeUpdater& updater, UI::UINodeId popup)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isPopupOpen";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto open = updater.isPopupOpen(popup);
    if (!open)
    {
        return Core::failure(rememberFirstError(std::move(open.error()), Operation));
    }
    return *open;
}

Core::Result<UI::UIPopupMetrics> PrimaryWindowUICapabilityState::popupMetrics(u64 epoch,
                                                                             PrimaryWindowUIPhase phase,
                                                                             const UI::UITreeUpdater& updater,
                                                                             UI::UINodeId popup)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::popupMetrics";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto metrics = updater.popupMetrics(popup);
    if (!metrics)
    {
        return Core::failure(rememberFirstError(std::move(metrics.error()), Operation));
    }
    return *metrics;
}

Core::Status PrimaryWindowUICapabilityState::setDropdownOpen(u64 epoch, PrimaryWindowUIPhase phase,
                                                             UI::UITreeUpdater& updater, UI::UINodeId dropdown,
                                                             bool open)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setDropdownOpen";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setDropdownOpen(dropdown, open);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<bool> PrimaryWindowUICapabilityState::isDropdownOpen(u64 epoch, PrimaryWindowUIPhase phase,
                                                                 const UI::UITreeUpdater& updater,
                                                                 UI::UINodeId dropdown)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isDropdownOpen";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto open = updater.isDropdownOpen(dropdown);
    if (!open)
    {
        return Core::failure(rememberFirstError(std::move(open.error()), Operation));
    }
    return *open;
}

Core::Status PrimaryWindowUICapabilityState::setDropdownSelectedItem(u64 epoch, PrimaryWindowUIPhase phase,
                                                                     UI::UITreeUpdater& updater,
                                                                     UI::UINodeId dropdown, UI::UINodeId item)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setDropdownSelectedItem";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setDropdownSelectedItem(dropdown, item);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UINodeId> PrimaryWindowUICapabilityState::dropdownSelectedItem(
    u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId dropdown)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::dropdownSelectedItem";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto selected = updater.dropdownSelectedItem(dropdown);
    if (!selected)
    {
        return Core::failure(rememberFirstError(std::move(selected.error()), Operation));
    }
    return *selected;
}

Core::Result<bool> PrimaryWindowUICapabilityState::isDropdownItemSelected(u64 epoch,
                                                                          PrimaryWindowUIPhase phase,
                                                                          const UI::UITreeUpdater& updater,
                                                                          UI::UINodeId item)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::isDropdownItemSelected";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto selected = updater.isDropdownItemSelected(item);
    if (!selected)
    {
        return Core::failure(rememberFirstError(std::move(selected.error()), Operation));
    }
    return *selected;
}

Core::Status PrimaryWindowUICapabilityState::setDropdownPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                              UI::UITreeUpdater& updater, UI::UINodeId dropdown,
                                                              const UI::UIDropdownPaint& paint)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::setDropdownPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return status;
    }
    Core::Status status = updater.setDropdownPaint(dropdown, paint);
    if (!status)
    {
        return Core::failure(rememberFirstError(std::move(status.error()), Operation));
    }
    return Core::success();
}

Core::Result<UI::UIDropdownPaint> PrimaryWindowUICapabilityState::dropdownPaint(u64 epoch,
                                                                                PrimaryWindowUIPhase phase,
                                                                                const UI::UITreeUpdater& updater,
                                                                                UI::UINodeId dropdown)
{
    constexpr std::string_view Operation = "PrimaryWindowUITreeUpdater::dropdownPaint";
    if (Core::Status status = validate(epoch, phase, true, Operation); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto paint = updater.dropdownPaint(dropdown);
    if (!paint)
    {
        return Core::failure(rememberFirstError(std::move(paint.error()), Operation));
    }
    return *paint;
}

Core::Status PrimaryWindowUICapabilityState::setProgressBarRange(u64 epoch, PrimaryWindowUIPhase phase,
                                                                 UI::UITreeUpdater& updater, UI::UINodeId progressBar,
                                                                 float minValue, float maxValue)
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

Core::Status PrimaryWindowUICapabilityState::setProgressBarValue(u64 epoch, PrimaryWindowUIPhase phase,
                                                                 UI::UITreeUpdater& updater, UI::UINodeId progressBar,
                                                                 float value)
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

Core::Result<float> PrimaryWindowUICapabilityState::progressBarValue(u64 epoch, PrimaryWindowUIPhase phase,
                                                                     const UI::UITreeUpdater& updater,
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

Core::Status PrimaryWindowUICapabilityState::setProgressBarPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                                 UI::UITreeUpdater& updater, UI::UINodeId progressBar,
                                                                 const UI::UIProgressBarPaint& paint)
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

Core::Result<UI::UIProgressBarPaint> PrimaryWindowUICapabilityState::progressBarPaint(u64 epoch,
                                                                                      PrimaryWindowUIPhase phase,
                                                                                      const UI::UITreeUpdater& updater,
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

Core::Status PrimaryWindowUICapabilityState::setRadioButtonPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                                 UI::UITreeUpdater& updater, UI::UINodeId radioButton,
                                                                 const UI::UIRadioButtonPaint& paint)
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

Core::Result<UI::UIRadioButtonPaint> PrimaryWindowUICapabilityState::radioButtonPaint(u64 epoch,
                                                                                      PrimaryWindowUIPhase phase,
                                                                                      const UI::UITreeUpdater& updater,
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

Core::Status PrimaryWindowUICapabilityState::setRadioButtonAction(u64 epoch, PrimaryWindowUIPhase phase,
                                                                  UI::UITreeUpdater& updater, UI::UINodeId radioButton,
                                                                  UI::UIButtonActionCallback callback)
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

Core::Status PrimaryWindowUICapabilityState::clearRadioButtonAction(u64 epoch, PrimaryWindowUIPhase phase,
                                                                    UI::UITreeUpdater& updater,
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

Core::Status PrimaryWindowUICapabilityState::setRadioButtonSelected(u64 epoch, PrimaryWindowUIPhase phase,
                                                                    UI::UITreeUpdater& updater,
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

Core::Result<bool> PrimaryWindowUICapabilityState::isRadioButtonSelected(u64 epoch, PrimaryWindowUIPhase phase,
                                                                         const UI::UITreeUpdater& updater,
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

Core::Result<bool> PrimaryWindowUICapabilityState::isRadioButtonPressed(u64 epoch, PrimaryWindowUIPhase phase,
                                                                        const UI::UITreeUpdater& updater,
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
