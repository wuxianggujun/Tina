#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace Tina::UI {

inline constexpr usize UISnackbarMaximumQueueCapacity = 4;
inline constexpr usize UISnackbarMaximumMessageBytes = 255;
inline constexpr usize UISnackbarMaximumActionLabelBytes = 47;

enum class UISnackbarTone : u8 {
    Neutral = 0,
    Success,
    Warning,
    Error,
};

enum class UISnackbarPhase : u8 {
    Hidden = 0,
    Entering,
    Visible,
    Exiting,
};

struct UISnackbarHostConfig final {
    usize queueCapacity = UISnackbarMaximumQueueCapacity;
    Core::Duration enterDuration{0.12};
    Core::Duration exitDuration{0.10};
    Core::Duration defaultVisibleDuration{4.0};
    UILayoutStyle layout{};
    UILayoutStyle surfaceLayout{};
    float viewportMargin = 16.0F;
};

struct UISnackbarMessage final {
    std::string_view text{};
    std::optional<std::string_view> actionLabel{};
    u64 actionToken = 0;
    UISnackbarTone tone = UISnackbarTone::Neutral;
    std::optional<Core::Duration> visibleDuration{};
};

struct UISnackbarPresentation final {
    std::string_view text{};
    std::string_view actionLabel{};
    u64 actionToken = 0;
    u64 revision = 0;
    UISnackbarTone tone = UISnackbarTone::Neutral;
    UISnackbarPhase phase = UISnackbarPhase::Hidden;

    [[nodiscard]] constexpr bool visible() const noexcept
    {
        return phase != UISnackbarPhase::Hidden;
    }

    [[nodiscard]] constexpr bool hasAction() const noexcept
    {
        return actionToken != 0 && !actionLabel.empty();
    }
};

struct UISnackbarHostParts final {
    UINodeId root{};
    UINodeId surface{};
    UINodeId toneBar{};
    UINodeId message{};
    UINodeId action{};

    auto operator<=>(const UISnackbarHostParts&) const = default;
};

// Caller-owned fixed-capacity state. All text is copied into bounded inline
// storage, timing is driven only by an explicit monotonic sample, and showing a
// message never requests focus. The returned presentation views remain valid
// until the next mutating call.
class UISnackbarHost final {
  public:
    [[nodiscard]] static Core::Result<UISnackbarHost>
    Create(const UISnackbarHostConfig& config) noexcept;

    [[nodiscard]] Core::Status enqueue(
        const UISnackbarMessage& message, Core::MonotonicTimePoint now) noexcept;
    [[nodiscard]] bool update(Core::MonotonicTimePoint now) noexcept;
    void dismiss(Core::MonotonicTimePoint now) noexcept;
    [[nodiscard]] std::optional<u64>
    activateAction(Core::MonotonicTimePoint now) noexcept;

    [[nodiscard]] UISnackbarPresentation presentation() const noexcept;
    [[nodiscard]] usize queuedCount() const noexcept { return count_; }
    [[nodiscard]] const UISnackbarHostConfig& config() const noexcept
    {
        return config_;
    }

  private:
    struct StoredMessage final {
        std::array<char, UISnackbarMaximumMessageBytes + 1U> text{};
        std::array<char, UISnackbarMaximumActionLabelBytes + 1U> actionLabel{};
        usize textBytes = 0;
        usize actionLabelBytes = 0;
        u64 actionToken = 0;
        UISnackbarTone tone = UISnackbarTone::Neutral;
        Core::Duration visibleDuration{};
    };

    explicit UISnackbarHost(UISnackbarHostConfig config) noexcept
        : config_(config)
    {
    }

    [[nodiscard]] StoredMessage* front() noexcept;
    [[nodiscard]] const StoredMessage* front() const noexcept;
    void beginFront(Core::MonotonicTimePoint now) noexcept;
    void beginExit(Core::MonotonicTimePoint now) noexcept;
    void popFront(Core::MonotonicTimePoint now) noexcept;
    void advanceRevision() noexcept;

    UISnackbarHostConfig config_{};
    std::array<StoredMessage, UISnackbarMaximumQueueCapacity> queue_{};
    usize head_ = 0;
    usize count_ = 0;
    u64 revision_ = 0;
    UISnackbarPhase phase_ = UISnackbarPhase::Hidden;
    Core::MonotonicTimePoint phaseStarted_{};
};

[[nodiscard]] Core::Result<UIComponentBuildBudget>
requiredSnackbarHostBuildBudget(const UISnackbarHostConfig& config) noexcept;

} // namespace Tina::UI
