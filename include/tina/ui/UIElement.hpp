#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIBehavior.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIFocus.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UIIcon.hpp>
#include <tina/ui/UIImage.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UISemantics.hpp>
#include <tina/ui/UISplitView.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UITooltip.hpp>
#include <tina/ui/UITreeView.hpp>

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
