#include <tina/runtime/PrimaryWindowUI.hpp>

#include "ui/PrimaryWindowUICapabilityState.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <string_view>
#include <utility>

namespace Tina {
namespace {

template <typename Value> [[nodiscard]] Core::Result<Value> expiredFacade(std::string_view operation)
{
    Core::Error error{RuntimeErrorCode::UIPhaseCapabilityExpired, "The primary-window UI capability has expired"};
    error.addContext(operation, "moved-from or detached facade");
    return Core::failure(std::move(error));
}

} // namespace

PrimaryWindowUITreeUpdater::PrimaryWindowUITreeUpdater(Runtime::Detail::PrimaryWindowUICapabilityState& state,
                                                       u64 epoch, Runtime::Detail::PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater updater) noexcept
    : m_state(&state), m_epoch(epoch), m_phase(phase), m_updater(std::move(updater))
{
}

PrimaryWindowUITreeUpdater::PrimaryWindowUITreeUpdater(PrimaryWindowUITreeUpdater&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)), m_epoch(std::exchange(other.m_epoch, 0)),
      m_phase(std::exchange(other.m_phase, Runtime::Detail::PrimaryWindowUIPhase::None)),
      m_updater(std::move(other.m_updater))
{
}

PrimaryWindowUITreeUpdater& PrimaryWindowUITreeUpdater::operator=(PrimaryWindowUITreeUpdater&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    m_state = std::exchange(other.m_state, nullptr);
    m_epoch = std::exchange(other.m_epoch, 0);
    m_phase = std::exchange(other.m_phase, Runtime::Detail::PrimaryWindowUIPhase::None);
    m_updater = std::move(other.m_updater);
    return *this;
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isAlive(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isAlive");
    }
    return m_state->isAlive(m_epoch, m_phase, m_updater, node);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createPanel(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createPanel");
    }
    return m_state->createPanel(m_epoch, m_phase, m_updater, parent);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createLabel(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createLabel");
    }
    return m_state->createLabel(m_epoch, m_phase, m_updater, parent);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createButton(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createButton");
    }
    return m_state->createButton(m_epoch, m_phase, m_updater, parent);
}

Core::Status PrimaryWindowUITreeUpdater::setLayoutStyle(UI::UINodeId node, const UI::UILayoutStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setLayoutStyle");
    }
    return m_state->setLayoutStyle(m_epoch, m_phase, m_updater, node, style);
}

Core::Status PrimaryWindowUITreeUpdater::setPointerHitPolicy(UI::UINodeId node, UI::UIPointerHitPolicy policy)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setPointerHitPolicy");
    }
    return m_state->setPointerHitPolicy(m_epoch, m_phase, m_updater, node, policy);
}

Core::Status PrimaryWindowUITreeUpdater::destroy(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::destroy");
    }
    return m_state->destroy(m_epoch, m_phase, m_updater, node);
}

PrimaryWindowUIRootBuilder::PrimaryWindowUIRootBuilder(Runtime::Detail::PrimaryWindowUICapabilityState& state,
                                                       u64 epoch) noexcept
    : m_state(&state), m_epoch(epoch)
{
}

PrimaryWindowUIRootBuilder::PrimaryWindowUIRootBuilder(PrimaryWindowUIRootBuilder&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)), m_epoch(std::exchange(other.m_epoch, 0))
{
}

PrimaryWindowUIRootBuilder& PrimaryWindowUIRootBuilder::operator=(PrimaryWindowUIRootBuilder&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    m_state = std::exchange(other.m_state, nullptr);
    m_epoch = std::exchange(other.m_epoch, 0);
    return *this;
}

Core::Result<UI::UIRootOwner> PrimaryWindowUIRootBuilder::createRoot()
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIRootOwner>("PrimaryWindowUIRootBuilder::createRoot");
    }
    return m_state->createRoot(m_epoch);
}

Core::Result<PrimaryWindowUITreeUpdater> PrimaryWindowUIRootBuilder::treeUpdater(UI::UIRootOwner& rootOwner)
{
    if (m_state == nullptr)
    {
        return expiredFacade<PrimaryWindowUITreeUpdater>("PrimaryWindowUIRootBuilder::treeUpdater");
    }
    return m_state->treeUpdater(m_epoch, Runtime::Detail::PrimaryWindowUIPhase::GameStateEnter, rootOwner);
}

} // namespace Tina
