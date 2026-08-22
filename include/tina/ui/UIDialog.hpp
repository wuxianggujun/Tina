#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace Tina::UI {

inline constexpr usize UIDialogMaximumActionCount = 4;

// How the content region behaves once the surface has reached its maximum
// height. Clip keeps the content region a plain Panel; Scroll makes it a Scroll
// behavior owner so long content stays reachable while the header and action
// row remain pinned.
enum class UIDialogContentOverflow : u8 {
    Clip = 0,
    Scroll,
};

[[nodiscard]] constexpr bool
isValidDialogContentOverflow(UIDialogContentOverflow overflow) noexcept
{
    return overflow >= UIDialogContentOverflow::Clip &&
           overflow <= UIDialogContentOverflow::Scroll;
}

struct UIDialogActionConfig final {
    std::string_view text{};
    UIButtonVariant variant = UIButtonVariant::Text;
    bool enabled = true;

    auto operator<=>(const UIDialogActionConfig&) const = default;
};

// Sizing and chrome affordances resolved against the product theme once per
// build. Auto lengths take their theme default; explicit lengths win. The
// surface sizes to its content and is then clamped, so minWidth is a floor
// rather than a fixed width.
struct UIDialogStyle final {
    UILayoutLength minWidth{};
    UILayoutLength maxWidth{};
    UILayoutLength maxHeight{};
    float viewportMargin = 24.0F;
    UIDialogContentOverflow contentOverflow = UIDialogContentOverflow::Clip;
    // Subtle Divider between the content region and the action row. Only
    // emitted when the dialog has at least one action.
    bool showActionDivider = false;

    auto operator<=>(const UIDialogStyle&) const = default;
};

// Modal-based bounded component. Modal remains the sole barrier/focus-scope
// owner; Surface, Header, Content and action Buttons are ordinary child
// Elements. Callers author dialog-specific content by parenting nodes to
// UIDialogParts::content, which is created before the action row so appended
// children stay above the actions.
struct UIDialogConfig final {
    std::string_view title{};
    // Optional single paragraph placed first inside the content region.
    // Callers authoring their own content leave this empty.
    std::optional<std::string_view> body{};
    // Accessible name for the close Button. Required when the style requests a
    // close Button because its glyph is not a usable name.
    std::optional<std::string_view> closeButtonName{};
    // Borrowed only for requiredDialogBuildBudget()/buildDialog().
    std::span<const UIDialogActionConfig> actions{};
    UIDialogStyle style{};
    UILayoutStyle layout{};
    UILayoutStyle surfaceLayout{};
    UILayoutStyle contentLayout{};
};

struct UIDialogParts final {
    // Scrim, ModalBarrier and focus scope. Ignore hit policy keeps the scrim out
    // of the target path; the committed Modal barrier still consumes backdrop
    // input before it can reach lower content.
    UINodeId modal{};
    UINodeId surface{};
    UINodeId header{};
    UINodeId title{};
    UINodeId closeButton{};
    // Author custom dialog content here.
    UINodeId content{};
    // The optional default paragraph, when config.body was supplied.
    UINodeId body{};
    UINodeId actionDivider{};
    UINodeId actionRow{};
    std::array<UINodeId, UIDialogMaximumActionCount> actions{};
    usize actionCount = 0;

    [[nodiscard]] constexpr std::span<const UINodeId> actionButtons() const noexcept
    {
        return std::span<const UINodeId>(actions.data(), actionCount);
    }

    [[nodiscard]] constexpr bool hasCloseButton() const noexcept
    {
        return closeButton.hasValue();
    }

    [[nodiscard]] constexpr bool hasBody() const noexcept
    {
        return body.hasValue();
    }

    auto operator<=>(const UIDialogParts&) const = default;
};

// Exact fixed reservation. More than UIDialogMaximumActionCount actions, an
// empty title, empty action or close-button text, invalid UTF-8, a non-finite
// or negative margin, a non-finite style length, an invalid overflow mode, an
// invalid Button variant, or a close Button without an accessible name all fail
// before any mutation.
[[nodiscard]] Core::Result<UIComponentBuildBudget>
requiredDialogBuildBudget(const UIDialogConfig& config) noexcept;

} // namespace Tina::UI
