#pragma once

#include <tina/runtime/PrimaryWindowUI.hpp>

#include <optional>
#include <string_view>
#include <thread>

namespace Tina::Runtime::Detail {

enum class PrimaryWindowUIPhase : u8 {
    None,
    GameStateEnter,
    UIUpdate,
};

// Runtime-private epoch owner. One instance serializes all Game SDK access to
// the primary-window UIContext and retains the first facade error until the
// enclosing callback finishes.
class PrimaryWindowUICapabilityState final {
  public:
    PrimaryWindowUICapabilityState() noexcept;

    PrimaryWindowUICapabilityState(const PrimaryWindowUICapabilityState&) = delete;
    PrimaryWindowUICapabilityState& operator=(const PrimaryWindowUICapabilityState&) = delete;
    PrimaryWindowUICapabilityState(PrimaryWindowUICapabilityState&&) = delete;
    PrimaryWindowUICapabilityState& operator=(PrimaryWindowUICapabilityState&&) = delete;

    [[nodiscard]] Core::Result<u64> beginGameStateEnterPhase(UI::UIContext* context);
    [[nodiscard]] Core::Result<u64> beginUIUpdatePhase(UI::UIContext* context);
    [[nodiscard]] Core::Status finishPhase(u64 epoch, PrimaryWindowUIPhase phase);
    void abortPhase(u64 epoch, PrimaryWindowUIPhase phase) noexcept;

    [[nodiscard]] bool hasPrimaryWindowUI(u64 epoch, PrimaryWindowUIPhase phase) const noexcept;
    [[nodiscard]] Core::Result<PrimaryWindowUIRootBuilder> rootBuilder(u64 epoch);
    [[nodiscard]] Core::Result<PrimaryWindowUITreeUpdater> treeUpdater(u64 epoch, PrimaryWindowUIPhase phase,
                                                                       UI::UIRootOwner& rootOwner);

    [[nodiscard]] Core::Result<UI::UIRootOwner> createRoot(u64 epoch);
    [[nodiscard]] Core::Result<bool> isAlive(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                                             UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UINodeId> createPanel(u64 epoch, PrimaryWindowUIPhase phase,
                                                         UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createLabel(u64 epoch, PrimaryWindowUIPhase phase,
                                                         UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createButton(u64 epoch, PrimaryWindowUIPhase phase,
                                                          UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Status setLayoutStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId node, const UI::UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                   UI::UINodeId node, UI::UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setBoxPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                           UI::UINodeId node, const UI::UIBoxPaint& paint);
    [[nodiscard]] Core::Result<UI::UIRoutedPointerListenerToken>
    addRoutedPointerListener(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                             UI::UIRoutedPointerListenerDesc descriptor, UI::UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Status destroy(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                       UI::UINodeId node);

  private:
    [[nodiscard]] Core::Result<u64> beginPhase(PrimaryWindowUIPhase phase, UI::UIContext* context);
    [[nodiscard]] Core::Status validate(u64 epoch, PrimaryWindowUIPhase phase, bool requireContext,
                                        std::string_view operation);
    [[nodiscard]] Core::Error rememberFirstError(Core::Error error, std::string_view operation);

    std::thread::id ownerThreadId_{};
    UI::UIContext* context_ = nullptr;
    u64 epoch_ = 0;
    PrimaryWindowUIPhase phase_ = PrimaryWindowUIPhase::None;
    std::optional<Core::Error> firstError_;
};

} // namespace Tina::Runtime::Detail
