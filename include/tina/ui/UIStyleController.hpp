#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UITheme.hpp>

#include <span>

namespace Tina::UI {

class UIContext;

class UIStyleController final {
  public:
    [[nodiscard]] const UITheme& productTheme() const noexcept;
    [[nodiscard]] Core::Status setProductTheme(const UITheme& theme);
    [[nodiscard]] Core::Result<UIStyleClassId> registerStyleClass();
    [[nodiscard]] Core::Result<UIStyleTokenId>
    registerStyleColorToken(UIStraightSrgba8Color value);
    [[nodiscard]] Core::Status installStyleSheet(
        std::span<const UIStyleBoxFillRule> rules);
    [[nodiscard]] Core::Result<UIStraightSrgba8Color>
    styleColorToken(UIStyleTokenId token) const;
    [[nodiscard]] Core::Status setStyleColorToken(
        UIStyleTokenId token, UIStraightSrgba8Color value);

  private:
    friend class UIContext;

    explicit UIStyleController(UIContext& context) noexcept : m_context(&context) {}

    UIContext* m_context = nullptr;
};

} // namespace Tina::UI
