#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIText.hpp>

#include <string_view>

namespace Tina::Runtime::Detail {

enum class PrimaryWindowUIPhase : u8;
class PrimaryWindowUICapabilityState;

} // namespace Tina::Runtime::Detail

namespace Tina {

// Move-only, callback-scoped access to the retained tree owned by one primary-
// window UI root. Every operation validates the Runtime phase epoch before it
// reaches UIContext; store UIRootOwner/UINodeId, not this facade.
class PrimaryWindowUITreeUpdater final {
  public:
    PrimaryWindowUITreeUpdater(const PrimaryWindowUITreeUpdater&) = delete;
    PrimaryWindowUITreeUpdater& operator=(const PrimaryWindowUITreeUpdater&) = delete;

    PrimaryWindowUITreeUpdater(PrimaryWindowUITreeUpdater&& other) noexcept;
    PrimaryWindowUITreeUpdater& operator=(PrimaryWindowUITreeUpdater&& other) noexcept;

    [[nodiscard]] Core::Result<bool> isAlive(UI::UINodeId node) const;
    [[nodiscard]] Core::Result<UI::UINodeId> createPanel(UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createLabel(UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createButton(UI::UINodeId parent);
    [[nodiscard]] Core::Status setLayoutStyle(UI::UINodeId node, const UI::UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(UI::UINodeId node, UI::UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setBoxPaint(UI::UINodeId node, const UI::UIBoxPaint& paint);
    [[nodiscard]] Core::Status setText(UI::UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyle(UI::UINodeId node, const UI::UITextStyle& style);
    [[nodiscard]] Core::Result<std::string_view> text(UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UITextStyle> textStyle(UI::UINodeId node);
    [[nodiscard]] Core::Status setButtonAction(UI::UINodeId button, UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearButtonAction(UI::UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressed(UI::UINodeId button) const;
    [[nodiscard]] Core::Result<UI::UIRoutedPointerListenerToken>
    addRoutedPointerListener(UI::UIRoutedPointerListenerDesc descriptor, UI::UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Status destroy(UI::UINodeId node);

  private:
    PrimaryWindowUITreeUpdater(Runtime::Detail::PrimaryWindowUICapabilityState& state, u64 epoch,
                               Runtime::Detail::PrimaryWindowUIPhase phase, UI::UITreeUpdater updater) noexcept;

    Runtime::Detail::PrimaryWindowUICapabilityState* m_state = nullptr;
    u64 m_epoch = 0;
    Runtime::Detail::PrimaryWindowUIPhase m_phase{};
    UI::UITreeUpdater m_updater{};

    friend class Runtime::Detail::PrimaryWindowUICapabilityState;
};

// Move-only, GameStateEnter-only capability for creating retained roots. The
// returned UIRootOwner is the persistent ownership token; this builder expires
// unconditionally when the onEnter callback returns.
class PrimaryWindowUIRootBuilder final {
  public:
    PrimaryWindowUIRootBuilder(const PrimaryWindowUIRootBuilder&) = delete;
    PrimaryWindowUIRootBuilder& operator=(const PrimaryWindowUIRootBuilder&) = delete;

    PrimaryWindowUIRootBuilder(PrimaryWindowUIRootBuilder&& other) noexcept;
    PrimaryWindowUIRootBuilder& operator=(PrimaryWindowUIRootBuilder&& other) noexcept;

    [[nodiscard]] Core::Result<UI::UIRootOwner> createRoot();
    [[nodiscard]] Core::Result<PrimaryWindowUITreeUpdater> treeUpdater(UI::UIRootOwner& rootOwner);

  private:
    PrimaryWindowUIRootBuilder(Runtime::Detail::PrimaryWindowUICapabilityState& state, u64 epoch) noexcept;

    Runtime::Detail::PrimaryWindowUICapabilityState* m_state = nullptr;
    u64 m_epoch = 0;

    friend class Runtime::Detail::PrimaryWindowUICapabilityState;
};

} // namespace Tina
