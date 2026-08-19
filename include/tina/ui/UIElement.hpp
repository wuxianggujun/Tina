#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIBehavior.hpp>
#include <tina/ui/UIBadge.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIDivider.hpp>
#include <tina/ui/UIFocus.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UIIcon.hpp>
#include <tina/ui/UIImage.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIMenu.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UISemantics.hpp>
#include <tina/ui/UISplitView.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UISurface.hpp>
#include <tina/ui/UITabView.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UIToggleSwitch.hpp>
#include <tina/ui/UITooltip.hpp>
#include <tina/ui/UITreeView.hpp>

#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace Tina::UI {

struct UIElementVisual final {
    UIStyleRoleId styleRole = UIStyleRoleId::None;
    // Borrowed only for createElement(); at most four registered ids are copied
    // into Context-owned fixed-capacity node links before the node is returned.
    std::span<const UIStyleClassId> styleClasses{};
    std::optional<UIBoxPaint> boxPaint{};
    // Borrowed only for createElement(); commands are copied into the
    // UIContext-owned fixed-capacity canvas pool before the node is returned.
    std::span<const UICanvasCommand> canvas{};
};

// One descriptor initializes the Element before it becomes observable through
// a committed snapshot. Borrowed text and canvas payloads are copied into
// UIContext-owned fixed-capacity storage during createElement().
struct UIElementDescriptor final {
    UILayoutStyle layout{};
    std::optional<std::string_view> text{};
    std::optional<UIImageContent> image{};
    std::optional<UITooltipConfig> tooltip{};
    std::optional<UISplitViewConfig> splitView{};
    std::optional<UISplitterConfig> splitter{};
    std::optional<UITabViewConfig> tabView{};
    std::optional<UITabConfig> tab{};
    std::optional<UIMenuConfig> menu{};
    std::optional<UIMenuItemConfig> menuItem{};
    std::optional<UITextStyle> textStyle{};
    UIContentAlignment contentAlignment{};
    UIElementVisual visual{};
    UIElementBehavior behaviors = UIElementBehavior::None;
    UISemanticsDescriptor semantics{};
    std::optional<UIPointerHitPolicy> pointerHitPolicy{};
    std::optional<UIFocusScopeMode> focusScopeMode{};
    bool enabled = true;
    UIListViewCreateConfig listView{};
    UITreeViewCreateConfig treeView{};
    // Applied only when this descriptor creates a TextInput TextEdit.
    UITextEditMultilineConfig textEditMultiline{};
};

[[nodiscard]] constexpr UIElementDescriptor makeImageElement(
    UIImageContent image, std::string_view accessibleName,
    UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .image = image,
        .contentAlignment = image.alignment,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Image,
            .name = accessibleName,
            .readOnly = true,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeIconElement(
    UIIconContent icon, UILayoutStyle layout = {}) noexcept
{
    const UIImageContent image{
        .source = icon.source,
        .fit = UIImageFit::Contain,
        .alignment = icon.alignment,
        .tint = icon.tint,
        .sampling = icon.sampling,
    };
    return UIElementDescriptor{
        .layout = layout,
        .image = image,
        .contentAlignment = image.alignment,
        .semantics = {
            .mode = UISemanticsMode::Exclude,
        },
        .pointerHitPolicy = UIPointerHitPolicy::Ignore,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeTooltipElement(
    std::string_view text, UITooltipConfig config = {},
    UILayoutStyle layout = {}) noexcept
{
    layout.placement = UILayoutPlacement::Overlay;
    if (layout.padding == UIEdgeSpacing{})
    {
        layout.padding = UIEdgeSpacing::HorizontalVertical(8.0F, 4.0F);
    }
    return UIElementDescriptor{
        .layout = layout,
        .text = text,
        .tooltip = config,
        .contentAlignment = {
            .horizontal = UIAxisAlignment::Start,
            .vertical = UIAxisAlignment::Center,
        },
        .visual = {.styleRole = UIStyleRoleId::TooltipSurface},
        .semantics = {.mode = UISemanticsMode::Exclude},
        .pointerHitPolicy = UIPointerHitPolicy::Ignore,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeSplitViewElement(
    UISplitViewConfig config = {}, UILayoutStyle layout = {}) noexcept
{
    layout.flexContainer.direction =
        config.orientation == UISplitViewOrientation::Horizontal
            ? UIFlexDirection::Row
            : UIFlexDirection::Column;
    return UIElementDescriptor{
        .layout = layout,
        .splitView = config,
        .semantics = {.mode = UISemanticsMode::Automatic},
        .pointerHitPolicy = UIPointerHitPolicy::Ignore,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeSplitterElement(
    UISplitterConfig config = {}, UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .splitter = config,
        .visual = {.styleRole = UIStyleRoleId::PanelSurface},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::RangeInput,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Slider,
            .actions = UISemanticsAction::Focus | UISemanticsAction::SetRangeValue,
        },
        .pointerHitPolicy = UIPointerHitPolicy::Targetable,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeTabViewElement(
    UITabViewConfig config = {}, UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .tabView = config,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::TabList,
        },
        .pointerHitPolicy = UIPointerHitPolicy::Ignore,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeTabElement(
    std::string_view text = {}, UITabConfig config = {}, UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .text = text,
        .tab = config,
        .contentAlignment = {
            .horizontal = UIAxisAlignment::Center,
            .vertical = UIAxisAlignment::Center,
        },
        .visual = {.styleRole = UIStyleRoleId::Tab},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::Activate,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Tab,
            .actions = UISemanticsAction::Focus | UISemanticsAction::Activate,
            .useContentAsName = true,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeMenuElement(
    UIMenuConfig config = {}, UILayoutStyle layout = {}) noexcept
{
    layout.placement = UILayoutPlacement::Overlay;
    layout.flexContainer.direction = UIFlexDirection::Column;
    if (layout.padding == UIEdgeSpacing{})
    {
        layout.padding = UIEdgeSpacing::All(4.0F);
    }
    return UIElementDescriptor{
        .layout = layout,
        .menu = config,
        .visual = {.styleRole = UIStyleRoleId::MenuSurface},
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Menu,
        },
        .pointerHitPolicy = UIPointerHitPolicy::Ignore,
        .focusScopeMode = UIFocusScopeMode::Contain,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeMenuItemElement(
    std::string_view text = {}, UIMenuItemConfig config = {},
    UILayoutStyle layout = {}) noexcept
{
    if (layout.padding == UIEdgeSpacing{})
    {
        layout.padding = UIEdgeSpacing::HorizontalVertical(8.0F, 6.0F);
    }
    const bool separator = config.kind == UIMenuItemKind::Separator;
    const UIElementBehavior behaviors = separator
                                            ? UIElementBehavior::None
                                            : UIElementBehavior::Focusable |
                                                  UIElementBehavior::Activate;
    const bool check = config.kind == UIMenuItemKind::Check;
    const bool radio = config.kind == UIMenuItemKind::Radio;
    return UIElementDescriptor{
        .layout = layout,
        .text = separator ? std::optional<std::string_view>{}
                          : std::optional<std::string_view>{text},
        .menuItem = config,
        .contentAlignment = separator
                              ? UIContentAlignment{}
                              : UIContentAlignment{
                                    .horizontal = UIAxisAlignment::Start,
                                    .vertical = UIAxisAlignment::Center,
                                },
        .visual = {.styleRole = UIStyleRoleId::MenuItem},
        .behaviors = behaviors,
        .semantics = separator
                         ? UISemanticsDescriptor{.mode = UISemanticsMode::Exclude}
                         : UISemanticsDescriptor{
                               .mode = UISemanticsMode::Publish,
                               .role = UISemanticsRole::MenuItem,
                               .actions = UIElementBehavior::None == behaviors
                                              ? UISemanticsAction::None
                                              : UISemanticsAction::Focus |
                                                    UISemanticsAction::Activate |
                                                    ((check || radio) ? UISemanticsAction::Toggle
                                                                      : UISemanticsAction::None),
                               .useContentAsName = true,
                           },
        .pointerHitPolicy = separator ? std::optional{UIPointerHitPolicy::Ignore}
                                      : std::optional{UIPointerHitPolicy::Targetable},
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeSurfaceElement(
    UISurfaceConfig config = {}, UILayoutStyle layout = {}) noexcept
{
    UIStyleRoleId role = UIStyleRoleId::None;
    switch (config.variant)
    {
    case UISurfaceVariant::Plain:
        role = UIStyleRoleId::None;
        break;
    case UISurfaceVariant::Filled:
        role = UIStyleRoleId::PanelSurface;
        break;
    case UISurfaceVariant::Elevated:
        role = UIStyleRoleId::PanelElevated;
        break;
    default:
        role = static_cast<UIStyleRoleId>(0xFFU);
        break;
    }
    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = role},
        .semantics = {.mode = UISemanticsMode::Automatic},
        .pointerHitPolicy = UIPointerHitPolicy::Ignore,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeDividerElement(
    UIDividerConfig config = {}, UILayoutStyle layout = {}) noexcept
{
    UIStyleRoleId role = UIStyleRoleId::DividerSubtle;
    switch (config.tone)
    {
    case UIDividerTone::Subtle:
        role = UIStyleRoleId::DividerSubtle;
        break;
    case UIDividerTone::Strong:
        role = UIStyleRoleId::DividerStrong;
        break;
    case UIDividerTone::Accent:
        role = UIStyleRoleId::DividerAccent;
        break;
    default:
        role = static_cast<UIStyleRoleId>(0xFFU);
        break;
    }

    switch (config.orientation)
    {
    case UIDividerOrientation::Horizontal:
        // Preserve an invalid thickness in the descriptor even when callers
        // provide an explicit height; normalizeLayoutStyle then rejects the
        // whole recipe instead of silently accepting a malformed profile.
        if (!(config.thickness >= 0.0F &&
              config.thickness < (std::numeric_limits<float>::infinity)()))
        {
            layout.size.height = UILayoutLength::Px(config.thickness);
        }
        if (layout.size.height.isAuto())
        {
            layout.size.height = UILayoutLength::Px(config.thickness);
        }
        break;
    case UIDividerOrientation::Vertical:
        if (!(config.thickness >= 0.0F &&
              config.thickness < (std::numeric_limits<float>::infinity)()))
        {
            layout.size.width = UILayoutLength::Px(config.thickness);
        }
        if (layout.size.width.isAuto())
        {
            layout.size.width = UILayoutLength::Px(config.thickness);
        }
        break;
    default:
        role = static_cast<UIStyleRoleId>(0xFFU);
        break;
    }

    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = role},
        .semantics = {.mode = UISemanticsMode::Exclude},
        .pointerHitPolicy = UIPointerHitPolicy::Ignore,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeBadgeElement(
    std::string_view text, UIBadgeConfig config = {},
    UILayoutStyle layout = {}) noexcept
{
    if (layout.padding == UIEdgeSpacing{})
    {
        layout.padding = UIEdgeSpacing::HorizontalVertical(8.0F, 3.0F);
    }

    UIStyleRoleId role = UIStyleRoleId::BadgeNeutral;
    switch (config.tone)
    {
    case UIBadgeTone::Neutral:
        role = UIStyleRoleId::BadgeNeutral;
        break;
    case UIBadgeTone::Accent:
        role = UIStyleRoleId::BadgeAccent;
        break;
    case UIBadgeTone::Danger:
        role = UIStyleRoleId::BadgeDanger;
        break;
    default:
        role = static_cast<UIStyleRoleId>(0xFFU);
        break;
    }

    return UIElementDescriptor{
        .layout = layout,
        .text = text,
        .contentAlignment = {
            .horizontal = UIAxisAlignment::Center,
            .vertical = UIAxisAlignment::Center,
        },
        .visual = {.styleRole = role},
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Label,
            .useContentAsName = true,
            .readOnly = true,
        },
        .pointerHitPolicy = UIPointerHitPolicy::Ignore,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeToggleSwitchElement(
    UIToggleSwitchConfig config = {}, UILayoutStyle layout = {}) noexcept
{
    UIStyleRoleId role = UIStyleRoleId::ToggleSwitch;
    switch (config.size)
    {
    case UIToggleSwitchSize::Compact:
        if (layout.size.width.isAuto())
        {
            layout.size.width = UILayoutLength::Px(36.0F);
        }
        if (layout.size.height.isAuto())
        {
            layout.size.height = UILayoutLength::Px(20.0F);
        }
        break;
    case UIToggleSwitchSize::Standard:
        if (layout.size.width.isAuto())
        {
            layout.size.width = UILayoutLength::Px(44.0F);
        }
        if (layout.size.height.isAuto())
        {
            layout.size.height = UILayoutLength::Px(24.0F);
        }
        break;
    default:
        role = static_cast<UIStyleRoleId>(0xFFU);
        break;
    }

    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = role},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::Activate |
                     UIElementBehavior::Toggle,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Switch,
            .name = config.accessibleName,
            .actions = UISemanticsAction::Focus | UISemanticsAction::Activate |
                       UISemanticsAction::Toggle,
        },
        .pointerHitPolicy = UIPointerHitPolicy::Targetable,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makePanelElement(UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{.layout = layout};
}

[[nodiscard]] constexpr UIElementDescriptor makeLabelElement(std::string_view text = {},
                                                             UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .text = text,
        .visual = {.styleRole = UIStyleRoleId::TextBody},
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Label,
            .useContentAsName = true,
            .readOnly = true,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeButtonElement(std::string_view text = {},
                                                              UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .text = text,
        .contentAlignment = {
            .horizontal = UIAxisAlignment::Center,
            .vertical = UIAxisAlignment::Center,
        },
        .visual = {.styleRole = UIStyleRoleId::ButtonTonal},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::Activate,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Button,
            .actions = UISemanticsAction::Focus | UISemanticsAction::Activate,
            .useContentAsName = true,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeCheckboxElement(UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = UIStyleRoleId::Checkbox},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::Activate | UIElementBehavior::Toggle,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Checkbox,
            .actions = UISemanticsAction::Focus | UISemanticsAction::Activate | UISemanticsAction::Toggle,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeSliderElement(UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = UIStyleRoleId::Slider},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::RangeInput,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Slider,
            .actions = UISemanticsAction::Focus | UISemanticsAction::SetRangeValue,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeTextEditElement(std::string_view text = {},
                                                                UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .text = text,
        .contentAlignment = {
            .horizontal = UIAxisAlignment::Start,
            .vertical = UIAxisAlignment::Center,
        },
        .visual = {.styleRole = UIStyleRoleId::TextInput},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::TextInput,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::TextEdit,
            .actions = UISemanticsAction::Focus | UISemanticsAction::SetTextValue,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeProgressBarElement(UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = UIStyleRoleId::ProgressBar},
        .behaviors = UIElementBehavior::ProgressValue,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::ProgressBar,
            .readOnly = true,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeRadioButtonElement(std::string_view text = {},
                                                                   UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .text = text,
        .contentAlignment = {
            .horizontal = UIAxisAlignment::Start,
            .vertical = UIAxisAlignment::Center,
        },
        .visual = {.styleRole = UIStyleRoleId::RadioButton},
        .behaviors =
            UIElementBehavior::Focusable | UIElementBehavior::Activate | UIElementBehavior::ExclusiveChoice,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::RadioButton,
            .actions = UISemanticsAction::Focus | UISemanticsAction::Activate | UISemanticsAction::Toggle,
            .useContentAsName = true,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeModalElement(UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = UIStyleRoleId::ModalSurface},
        .behaviors = UIElementBehavior::ModalBarrier,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Dialog,
        },
        .focusScopeMode = UIFocusScopeMode::Contain,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeScrollViewElement(UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = UIStyleRoleId::ScrollView},
        .behaviors = UIElementBehavior::Scroll,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::ScrollView,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeDropdownElement(std::string_view text = {},
                                                                UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .text = text,
        .contentAlignment = {
            .horizontal = UIAxisAlignment::Start,
            .vertical = UIAxisAlignment::Center,
        },
        .visual = {.styleRole = UIStyleRoleId::Dropdown},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::Activate | UIElementBehavior::Select,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::ComboBox,
            .actions = UISemanticsAction::Focus | UISemanticsAction::Activate,
            .useContentAsName = true,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makePopupElement(UILayoutStyle layout = {}) noexcept
{
    layout.placement = UILayoutPlacement::Overlay;
    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = UIStyleRoleId::PopupSurface},
        .behaviors = UIElementBehavior::Popup,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::List,
        },
        .focusScopeMode = UIFocusScopeMode::Contain,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeDropdownItemElement(std::string_view text = {},
                                                                    UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .text = text,
        .contentAlignment = {
            .horizontal = UIAxisAlignment::Start,
            .vertical = UIAxisAlignment::Center,
        },
        .visual = {.styleRole = UIStyleRoleId::CollectionItem},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::Activate | UIElementBehavior::SelectOption,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::ListItem,
            .actions = UISemanticsAction::Focus | UISemanticsAction::Activate,
            .useContentAsName = true,
        },
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeListViewElement(UIListViewCreateConfig config = {},
                                                                UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = UIStyleRoleId::ListView},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::VirtualList,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::List,
            .actions = UISemanticsAction::Focus,
        },
        .listView = config,
    };
}

[[nodiscard]] constexpr UIElementDescriptor makeTreeViewElement(UITreeViewCreateConfig config = {},
                                                                UILayoutStyle layout = {}) noexcept
{
    return UIElementDescriptor{
        .layout = layout,
        .visual = {.styleRole = UIStyleRoleId::TreeView},
        .behaviors = UIElementBehavior::Focusable | UIElementBehavior::VirtualTree,
        .semantics = {
            .mode = UISemanticsMode::Publish,
            .role = UISemanticsRole::Tree,
            .actions = UISemanticsAction::Focus,
        },
        .treeView = config,
    };
}

} // namespace Tina::UI
