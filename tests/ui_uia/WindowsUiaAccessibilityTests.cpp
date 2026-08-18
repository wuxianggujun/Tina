#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>
#include <tina/ui/WindowsUiaAccessibilityProviderFactory.hpp>

#include "UIUiaMapping.hpp"
#include "WindowsUiaAccessibilityProvider.hpp"
#include "WindowsUiaHostBridge.hpp"

#include <UIAutomation.h>
#include <Windows.h>
#include <wrl/client.h>

#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using Microsoft::WRL::ComPtr;

[[nodiscard]] std::unique_ptr<UI::UIContext>
createContext(Platform::WindowId window,
              UI::UIContextCapacityConfig capacities =
                  {
                      .nodeCapacity = 64,
                      .rootCapacity = 1,
                      .paintSnapshotCapacity = 64,
                      .routePathCapacity = 8,
                      .routedPointerListenerCapacity = 8,
                      .buttonActionCapacity = 8,
                  },
              std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

TEST(WindowsUiaMappingTest, RoleMapsToUiaControlTypeConstants)
{
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::Button), UI::Uia::kControlTypeButton);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::Checkbox), UI::Uia::kControlTypeCheckBox);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::Slider), UI::Uia::kControlTypeSlider);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::ProgressBar), UI::Uia::kControlTypeProgressBar);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::RadioButton), UI::Uia::kControlTypeRadioButton);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::TextEdit), UI::Uia::kControlTypeEdit);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::Label), UI::Uia::kControlTypeText);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::ScrollView), UI::Uia::kControlTypePane);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::Tree), UI::Uia::kControlTypeTree);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::TreeItem), UI::Uia::kControlTypeTreeItem);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::TabList), UI::Uia::kControlTypeTab);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::Tab), UI::Uia::kControlTypeTabItem);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::TabPanel), UI::Uia::kControlTypePane);
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::Group), UI::Uia::kControlTypeGroup);
}

TEST(WindowsUiaMappingTest, MapsEnabledFocusRangeToggleAndValue)
{
    UI::UIAccessibilityNode button{
        .role = UI::UISemanticsRole::Button,
        .actions = UI::UISemanticsAction::Focus | UI::UISemanticsAction::Activate,
        .name = "Apply",
        .states = UI::UIAccessibilityState::Enabled | UI::UIAccessibilityState::Focused,
    };
    const auto mappedButton = UI::Uia::mapAccessibilityNode(button);
    EXPECT_EQ(mappedButton.controlTypeId, UI::Uia::kControlTypeButton);
    EXPECT_EQ(mappedButton.name, "Apply");
    EXPECT_TRUE(mappedButton.isEnabled);
    EXPECT_TRUE(mappedButton.isKeyboardFocusable);
    EXPECT_TRUE(mappedButton.hasKeyboardFocus);
    EXPECT_FALSE(mappedButton.rangeValue.has_value());
    EXPECT_FALSE(mappedButton.toggleState.has_value());
    EXPECT_FALSE(mappedButton.value.has_value());

    UI::UIAccessibilityNode checkbox{
        .role = UI::UISemanticsRole::Checkbox,
        .actions = UI::UISemanticsAction::Focus | UI::UISemanticsAction::Toggle,
        .name = "Music",
        .states = UI::UIAccessibilityState::Enabled | UI::UIAccessibilityState::Checked,
    };
    const auto mappedCheckbox = UI::Uia::mapAccessibilityNode(checkbox);
    ASSERT_TRUE(mappedCheckbox.toggleState.has_value());
    EXPECT_EQ(*mappedCheckbox.toggleState, UI::Uia::kToggleStateOn);

    UI::UIAccessibilityNode slider{
        .role = UI::UISemanticsRole::Slider,
        .actions = UI::UISemanticsAction::Focus | UI::UISemanticsAction::SetRangeValue,
        .value = 0.4F,
        .minValue = 0.0F,
        .maxValue = 1.0F,
        .states = UI::UIAccessibilityState::Enabled | UI::UIAccessibilityState::HasRange,
    };
    const auto mappedSlider = UI::Uia::mapAccessibilityNode(slider);
    ASSERT_TRUE(mappedSlider.rangeValue.has_value());
    EXPECT_FLOAT_EQ(static_cast<float>(mappedSlider.rangeValue->value), 0.4F);
    EXPECT_FLOAT_EQ(static_cast<float>(mappedSlider.rangeValue->minimum), 0.0F);
    EXPECT_FLOAT_EQ(static_cast<float>(mappedSlider.rangeValue->maximum), 1.0F);
    EXPECT_FALSE(mappedSlider.rangeValue->isReadOnly);

    UI::UIAccessibilityNode progress{
        .role = UI::UISemanticsRole::ProgressBar,
        .value = 55.0F,
        .minValue = 0.0F,
        .maxValue = 100.0F,
        .states =
            UI::UIAccessibilityState::Enabled | UI::UIAccessibilityState::HasRange | UI::UIAccessibilityState::ReadOnly,
    };
    const auto mappedProgress = UI::Uia::mapAccessibilityNode(progress);
    ASSERT_TRUE(mappedProgress.rangeValue.has_value());
    EXPECT_TRUE(mappedProgress.rangeValue->isReadOnly);
    EXPECT_FALSE(mappedProgress.isKeyboardFocusable);

    UI::UIAccessibilityNode textEdit{
        .role = UI::UISemanticsRole::TextEdit,
        .actions = UI::UISemanticsAction::Focus | UI::UISemanticsAction::SetTextValue,
        .valueText = "Player",
        .states = UI::UIAccessibilityState::Enabled,
    };
    const auto mappedEdit = UI::Uia::mapAccessibilityNode(textEdit);
    ASSERT_TRUE(mappedEdit.value.has_value());
    EXPECT_EQ(mappedEdit.value->value, "Player");
    EXPECT_FALSE(mappedEdit.value->isReadOnly);
    EXPECT_TRUE(mappedEdit.isKeyboardFocusable);
}

TEST(WindowsUiaMappingTest, DisabledNodeIsNotKeyboardFocusable)
{
    UI::UIAccessibilityNode button{
        .role = UI::UISemanticsRole::Button,
        .actions = UI::UISemanticsAction::Focus | UI::UISemanticsAction::Activate,
        .name = "Off",
        .states = UI::UIAccessibilityState::Disabled | UI::UIAccessibilityState::Focused,
    };
    const auto mapped = UI::Uia::mapAccessibilityNode(button);
    EXPECT_FALSE(mapped.isEnabled);
    EXPECT_FALSE(mapped.isKeyboardFocusable);
    EXPECT_FALSE(mapped.hasKeyboardFocus);
}

TEST(WindowsUiaProviderTest, PublishesDropdownValueAndSelectedItem)
{
    ASSERT_TRUE(UI::windowsUiaAccessibilityProviderAvailable());
    auto providerResult = UI::createWindowsUiaAccessibilityProvider();
    ASSERT_TRUE(providerResult.has_value()) << providerResult.error().message;
    auto& provider = *providerResult;

    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    auto dropdown = updater.createElement(root.rootNodeId(), UI::makeDropdownElement());
    ASSERT_TRUE(dropdown.has_value()) << dropdown.error().message;
    auto popup = updater.createElement(*dropdown, UI::makePopupElement());
    ASSERT_TRUE(popup.has_value()) << popup.error().message;
    auto firstItem = updater.createElement(*popup, UI::makeDropdownItemElement());
    auto secondItem = updater.createElement(*popup, UI::makeDropdownItemElement());
    ASSERT_TRUE(firstItem.has_value()) << firstItem.error().message;
    ASSERT_TRUE(secondItem.has_value()) << secondItem.error().message;

    UI::UILayoutStyle popupLayout = fixedSize(160.0F, 56.0F);
    popupLayout.placement = UI::UILayoutPlacement::Overlay;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(400.0F, 300.0F)));
    assertOk(updater.setLayoutStyle(*dropdown, fixedSize(160.0F, 32.0F)));
    assertOk(updater.setLayoutStyle(*popup, popupLayout));
    assertOk(updater.setLayoutStyle(*firstItem, fixedSize(160.0F, 28.0F)));
    assertOk(updater.setLayoutStyle(*secondItem, fixedSize(160.0F, 28.0F)));
    assertOk(updater.setText(*dropdown, "Quality"));
    assertOk(updater.setText(*firstItem, "Balanced"));
    assertOk(updater.setText(*secondItem, "High"));
    assertOk(updater.setDropdownSelectedItem(*dropdown, *secondItem));
    assertOk(updater.setDropdownOpen(*dropdown, true));
    assertOk(context->commitLayout({.width = 400.0F, .height = 300.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->committedSemantics()));
    assertOk(provider->publish(tree));

    auto* uia = dynamic_cast<UI::WindowsUiaAccessibilityProvider*>(provider.get());
    ASSERT_NE(uia, nullptr);
    auto dropdownNode = uia->readMappedNode(*dropdown);
    ASSERT_TRUE(dropdownNode.has_value()) << dropdownNode.error().message;
    EXPECT_EQ(dropdownNode->controlTypeId, UI::Uia::kControlTypeComboBox);
    EXPECT_TRUE(dropdownNode->isKeyboardFocusable);
    ASSERT_TRUE(dropdownNode->value.has_value());
    EXPECT_EQ(dropdownNode->value->value, "High");
    EXPECT_TRUE(dropdownNode->value->isReadOnly);

    auto firstItemNode = uia->readMappedNode(*firstItem);
    ASSERT_TRUE(firstItemNode.has_value()) << firstItemNode.error().message;
    EXPECT_EQ(firstItemNode->controlTypeId, UI::Uia::kControlTypeListItem);
    EXPECT_FALSE(firstItemNode->isSelected);

    auto secondItemNode = uia->readMappedNode(*secondItem);
    ASSERT_TRUE(secondItemNode.has_value()) << secondItemNode.error().message;
    EXPECT_EQ(secondItemNode->controlTypeId, UI::Uia::kControlTypeListItem);
    EXPECT_TRUE(secondItemNode->isSelected);
    EXPECT_TRUE(secondItemNode->isKeyboardFocusable);
}

TEST(WindowsUiaProviderTest, FactoryAvailableAndPublishMapsWidgetProperties)
{
    ASSERT_TRUE(UI::windowsUiaAccessibilityProviderAvailable());
    auto providerResult = UI::createWindowsUiaAccessibilityProvider();
    ASSERT_TRUE(providerResult.has_value()) << providerResult.error().message;
    auto& provider = *providerResult;

    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());

    auto button = context->rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    auto checkbox = context->rootBuilder().createElement(root.rootNodeId(), UI::makeCheckboxElement());
    auto slider = context->rootBuilder().createElement(root.rootNodeId(), UI::makeSliderElement());
    auto progress = context->rootBuilder().createElement(root.rootNodeId(), UI::makeProgressBarElement());
    auto radio = context->rootBuilder().createElement(root.rootNodeId(), UI::makeRadioButtonElement());
    auto textEdit = context->rootBuilder().createElement(root.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(button.has_value());
    ASSERT_TRUE(checkbox.has_value());
    ASSERT_TRUE(slider.has_value());
    ASSERT_TRUE(progress.has_value());
    ASSERT_TRUE(radio.has_value());
    ASSERT_TRUE(textEdit.has_value());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(400.0F, 300.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 32.0F)));
    assertOk(updater.setLayoutStyle(*checkbox, fixedSize(24.0F, 24.0F)));
    assertOk(updater.setLayoutStyle(*slider, fixedSize(120.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(*progress, fixedSize(120.0F, 12.0F)));
    assertOk(updater.setLayoutStyle(*radio, fixedSize(100.0F, 24.0F)));
    assertOk(updater.setLayoutStyle(*textEdit, fixedSize(160.0F, 32.0F)));
    assertOk(updater.setText(*button, "Apply"));
    assertOk(updater.setChecked(*checkbox, true));
    assertOk(updater.setSliderRange(*slider, 0.0F, 1.0F, 0.1F));
    assertOk(updater.setSliderValue(*slider, 0.4F));
    assertOk(updater.setProgressBarRange(*progress, 0.0F, 100.0F));
    assertOk(updater.setProgressBarValue(*progress, 55.0F));
    assertOk(updater.setText(*radio, "Windowed"));
    assertOk(updater.setRadioButtonSelected(*radio, true));
    assertOk(updater.setText(*textEdit, "Player"));
    assertOk(context->commitLayout({.width = 400.0F, .height = 300.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->committedSemantics()));
    assertOk(provider->publish(tree));
    EXPECT_TRUE(provider->hasPublishedTree());

    auto* uia = dynamic_cast<UI::WindowsUiaAccessibilityProvider*>(provider.get());
    ASSERT_NE(uia, nullptr);
    EXPECT_EQ(uia->publishCount(), 1U);
    EXPECT_GE(uia->mappedNodes().size(), 6U);

    auto buttonNode = uia->readMappedNode(*button);
    ASSERT_TRUE(buttonNode.has_value()) << buttonNode.error().message;
    EXPECT_EQ(buttonNode->controlTypeId, UI::Uia::kControlTypeButton);
    EXPECT_EQ(buttonNode->name, "Apply");
    EXPECT_TRUE(buttonNode->isEnabled);
    EXPECT_TRUE(buttonNode->isKeyboardFocusable);

    auto checkboxNode = uia->readMappedNode(*checkbox);
    ASSERT_TRUE(checkboxNode.has_value());
    EXPECT_EQ(checkboxNode->controlTypeId, UI::Uia::kControlTypeCheckBox);
    ASSERT_TRUE(checkboxNode->toggleState.has_value());
    EXPECT_EQ(*checkboxNode->toggleState, UI::Uia::kToggleStateOn);

    auto sliderNode = uia->readMappedNode(*slider);
    ASSERT_TRUE(sliderNode.has_value());
    EXPECT_EQ(sliderNode->controlTypeId, UI::Uia::kControlTypeSlider);
    ASSERT_TRUE(sliderNode->rangeValue.has_value());
    EXPECT_FLOAT_EQ(static_cast<float>(sliderNode->rangeValue->value), 0.4F);
    EXPECT_FALSE(sliderNode->rangeValue->isReadOnly);

    auto progressNode = uia->readMappedNode(*progress);
    ASSERT_TRUE(progressNode.has_value());
    EXPECT_EQ(progressNode->controlTypeId, UI::Uia::kControlTypeProgressBar);
    ASSERT_TRUE(progressNode->rangeValue.has_value());
    EXPECT_FLOAT_EQ(static_cast<float>(progressNode->rangeValue->value), 55.0F);
    EXPECT_TRUE(progressNode->rangeValue->isReadOnly);

    auto radioNode = uia->readMappedNode(*radio);
    ASSERT_TRUE(radioNode.has_value());
    EXPECT_EQ(radioNode->controlTypeId, UI::Uia::kControlTypeRadioButton);
    ASSERT_TRUE(radioNode->toggleState.has_value());
    EXPECT_EQ(*radioNode->toggleState, UI::Uia::kToggleStateOn);

    auto textNode = uia->readMappedNode(*textEdit);
    ASSERT_TRUE(textNode.has_value());
    EXPECT_EQ(textNode->controlTypeId, UI::Uia::kControlTypeEdit);
    ASSERT_TRUE(textNode->value.has_value());
    EXPECT_EQ(textNode->value->value, "Player");
}

TEST(WindowsUiaProviderTest, StaleNodeAfterDestroyIsRejected)
{
    auto providerResult = UI::createWindowsUiaAccessibilityProvider();
    ASSERT_TRUE(providerResult.has_value());
    auto& provider = *providerResult;
    auto* uia = dynamic_cast<UI::WindowsUiaAccessibilityProvider*>(provider.get());
    ASSERT_NE(uia, nullptr);

    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());

    auto button = context->rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 32.0F)));
    assertOk(updater.setText(*button, "Doomed"));
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->committedSemantics()));
    assertOk(provider->publish(tree));
    const UI::UINodeId staleId = *button;
    ASSERT_TRUE(uia->readMappedNode(staleId).has_value());

    assertOk(updater.destroy(*button));
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));

    UI::UIAccessibilityTree fresh;
    assertOk(fresh.rebuildFrom(context->committedSemantics()));
    EXPECT_EQ(fresh.findNode(staleId), nullptr);
    assertOk(provider->publish(fresh));
    auto read = uia->readMappedNode(staleId);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, UI::UIErrorCode::AccessibilityNodeStale);
}

TEST(WindowsUiaProviderTest, ClearRemovesPublishedTreeOnRootRelease)
{
    auto providerResult = UI::createWindowsUiaAccessibilityProvider();
    ASSERT_TRUE(providerResult.has_value());
    auto& provider = *providerResult;
    auto* uia = dynamic_cast<UI::WindowsUiaAccessibilityProvider*>(provider.get());
    ASSERT_NE(uia, nullptr);

    UI::UIAccessibilityTree empty;
    assertOk(provider->publish(empty));
    EXPECT_TRUE(provider->hasPublishedTree());
    EXPECT_EQ(uia->publishCount(), 1U);

    provider->clear();
    EXPECT_FALSE(provider->hasPublishedTree());
    EXPECT_EQ(uia->clearCount(), 1U);
    EXPECT_TRUE(uia->mappedNodes().empty());
    auto read = uia->readMappedNode(UI::UINodeId{});
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, UI::UIErrorCode::AccessibilityTreeMissing);
}

TEST(WindowsUiaProviderTest, PublicHeadersStayComFree)
{
    // Compile-time / link-time smoke: factory header and UIAccessibility are used
    // without requiring COM types in the test translation unit public include set.
    EXPECT_TRUE(UI::windowsUiaAccessibilityProviderAvailable());
}

TEST(WindowsUiaHostBridgeTest, AttachPublishExposesRootProviderAndChildren)
{
    auto bridgeResult = UI::createWindowsUiaHostBridge();
    ASSERT_TRUE(bridgeResult.has_value());
    auto bridge = std::move(*bridgeResult);

    const HWND hwnd =
        ::CreateWindowExW(0, L"STATIC", L"TinaUiaBridgeTest", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320,
                          200, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    ASSERT_NE(hwnd, nullptr);

    assertOk(bridge->attach(hwnd));
    EXPECT_TRUE(bridge->isAttached());
    EXPECT_EQ(bridge->hwnd(), hwnd);

    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto button = context->rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 32.0F)));
    assertOk(updater.setText(*button, "Bridge"));
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->committedSemantics()));
    assertOk(bridge->publish(tree, *context));
    EXPECT_TRUE(bridge->hasPublishedTree());
    EXPECT_GE(bridge->publishCount(), 1U);

    IRawElementProviderSimple* provider = bridge->acquireRootProvider();
    ASSERT_NE(provider, nullptr);

    IRawElementProviderFragmentRoot* fragmentRoot = nullptr;
    ASSERT_HRESULT_SUCCEEDED(provider->QueryInterface(IID_PPV_ARGS(&fragmentRoot)));
    ASSERT_NE(fragmentRoot, nullptr);

    IRawElementProviderFragment* rootFragment = nullptr;
    ASSERT_HRESULT_SUCCEEDED(provider->QueryInterface(IID_PPV_ARGS(&rootFragment)));
    ASSERT_NE(rootFragment, nullptr);

    IRawElementProviderFragment* firstChild = nullptr;
    ASSERT_HRESULT_SUCCEEDED(rootFragment->Navigate(NavigateDirection_FirstChild, &firstChild));
    EXPECT_NE(firstChild, nullptr);
    if (firstChild != nullptr)
    {
        IRawElementProviderSimple* childSimple = nullptr;
        ASSERT_HRESULT_SUCCEEDED(firstChild->QueryInterface(IID_PPV_ARGS(&childSimple)));
        ASSERT_NE(childSimple, nullptr);
        VARIANT controlType{};
        ::VariantInit(&controlType);
        ASSERT_HRESULT_SUCCEEDED(childSimple->GetPropertyValue(UIA_ControlTypePropertyId, &controlType));
        EXPECT_EQ(controlType.vt, VT_I4);
        ::VariantClear(&controlType);
        UiaRect bounds{};
        ASSERT_HRESULT_SUCCEEDED(firstChild->get_BoundingRectangle(&bounds));
        POINT clientOrigin{0, 0};
        ASSERT_TRUE(::ClientToScreen(hwnd, &clientOrigin));
        const double contentScale =
            static_cast<double>(::GetDpiForWindow(hwnd)) / 96.0;
        EXPECT_NEAR(bounds.left, static_cast<double>(clientOrigin.x), 1.0);
        EXPECT_NEAR(bounds.top, static_cast<double>(clientOrigin.y), 1.0);
        EXPECT_NEAR(bounds.width, 80.0 * contentScale, 1.0);
        EXPECT_NEAR(bounds.height, 32.0 * contentScale, 1.0);
        childSimple->Release();
        firstChild->Release();
    }
    rootFragment->Release();
    fragmentRoot->Release();
    provider->Release();

    bridge->clear();
    EXPECT_GE(bridge->clearCount(), 1U);
    bridge->detach();
    EXPECT_FALSE(bridge->isAttached());
    (void)::DestroyWindow(hwnd);
}

TEST(WindowsUiaHostBridgeTest, PatternsMarshalActionsAndPreserveControlBehavior)
{
    auto bridgeResult = UI::createWindowsUiaHostBridge();
    ASSERT_TRUE(bridgeResult.has_value());
    auto bridge = std::move(*bridgeResult);
    const HWND hwnd =
        ::CreateWindowExW(0, L"STATIC", L"TinaUiaPatternTest", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          400, 260, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    ASSERT_NE(hwnd, nullptr);
    assertOk(bridge->attach(hwnd));

    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    auto button = updater.createElement(root.rootNodeId(), UI::makeButtonElement());
    auto checkbox = updater.createElement(root.rootNodeId(), UI::makeCheckboxElement());
    auto slider = updater.createElement(root.rootNodeId(), UI::makeSliderElement());
    auto progress = updater.createElement(root.rootNodeId(), UI::makeProgressBarElement());
    auto textEdit = updater.createElement(root.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(button.has_value());
    ASSERT_TRUE(checkbox.has_value());
    ASSERT_TRUE(slider.has_value());
    ASSERT_TRUE(progress.has_value());
    ASSERT_TRUE(textEdit.has_value());

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(360.0F, 220.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(100.0F, 32.0F)));
    assertOk(updater.setLayoutStyle(*checkbox, fixedSize(100.0F, 32.0F)));
    assertOk(updater.setLayoutStyle(*slider, fixedSize(160.0F, 24.0F)));
    assertOk(updater.setLayoutStyle(*progress, fixedSize(160.0F, 16.0F)));
    assertOk(updater.setLayoutStyle(*textEdit, fixedSize(180.0F, 32.0F)));
    assertOk(updater.setText(*button, "Apply"));
    assertOk(updater.setText(*textEdit, "Player"));
    assertOk(updater.setSliderRange(*slider, 0.0F, 100.0F, 1.0F));
    assertOk(updater.setProgressBarRange(*progress, 0.0F, 100.0F));

    u32 buttonActivations = 0;
    u32 checkboxActivations = 0;
    assertOk(updater.setButtonAction(
        *button, UI::UIButtonActionCallback([&](const UI::UIButtonActionEvent& event) noexcept {
            EXPECT_EQ(event.source, UI::UIButtonActivationSource::Accessibility);
            ++buttonActivations;
        })));
    assertOk(updater.setCheckboxAction(
        *checkbox, UI::UIButtonActionCallback([&](const UI::UIButtonActionEvent& event) noexcept {
            EXPECT_EQ(event.source, UI::UIButtonActivationSource::Accessibility);
            ++checkboxActivations;
        })));
    assertOk(context->commitLayout({.width = 360.0F, .height = 220.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->committedSemantics()));
    assertOk(bridge->publish(tree, *context));

    ComPtr<IRawElementProviderSimple> rootSimple;
    rootSimple.Attach(bridge->acquireRootProvider());
    ASSERT_NE(rootSimple.Get(), nullptr);
    ComPtr<IRawElementProviderFragment> rootFragment;
    ASSERT_HRESULT_SUCCEEDED(rootSimple.As(&rootFragment));

    ComPtr<IRawElementProviderFragment> buttonFragment;
    ASSERT_HRESULT_SUCCEEDED(rootFragment->Navigate(NavigateDirection_FirstChild, &buttonFragment));
    ASSERT_NE(buttonFragment.Get(), nullptr);
    ComPtr<IRawElementProviderSimple> buttonSimple;
    ASSERT_HRESULT_SUCCEEDED(buttonFragment.As(&buttonSimple));
    ComPtr<IUnknown> invokeUnknown;
    ASSERT_HRESULT_SUCCEEDED(buttonSimple->GetPatternProvider(UIA_InvokePatternId, &invokeUnknown));
    ASSERT_NE(invokeUnknown.Get(), nullptr);
    ComPtr<IInvokeProvider> invoke;
    ASSERT_HRESULT_SUCCEEDED(invokeUnknown.As(&invoke));
    EXPECT_HRESULT_SUCCEEDED(invoke->Invoke());
    EXPECT_EQ(buttonActivations, 1U);
    EXPECT_HRESULT_SUCCEEDED(buttonFragment->SetFocus());
    EXPECT_EQ(context->defaultActionFocus(), *button);

    ComPtr<IRawElementProviderFragment> checkboxFragment;
    ASSERT_HRESULT_SUCCEEDED(buttonFragment->Navigate(NavigateDirection_NextSibling, &checkboxFragment));
    ASSERT_NE(checkboxFragment.Get(), nullptr);
    ComPtr<IRawElementProviderSimple> checkboxSimple;
    ASSERT_HRESULT_SUCCEEDED(checkboxFragment.As(&checkboxSimple));
    ComPtr<IUnknown> toggleUnknown;
    ASSERT_HRESULT_SUCCEEDED(checkboxSimple->GetPatternProvider(UIA_TogglePatternId, &toggleUnknown));
    ASSERT_NE(toggleUnknown.Get(), nullptr);
    ComPtr<IToggleProvider> toggle;
    ASSERT_HRESULT_SUCCEEDED(toggleUnknown.As(&toggle));
    ToggleState toggleState = ToggleState_Indeterminate;
    EXPECT_HRESULT_SUCCEEDED(toggle->get_ToggleState(&toggleState));
    EXPECT_EQ(toggleState, ToggleState_Off);
    EXPECT_HRESULT_SUCCEEDED(toggle->Toggle());
    EXPECT_EQ(checkboxActivations, 1U);
    auto checked = updater.isChecked(*checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_TRUE(*checked);

    ComPtr<IRawElementProviderFragment> sliderFragment;
    ASSERT_HRESULT_SUCCEEDED(checkboxFragment->Navigate(NavigateDirection_NextSibling, &sliderFragment));
    ComPtr<IRawElementProviderSimple> sliderSimple;
    ASSERT_HRESULT_SUCCEEDED(sliderFragment.As(&sliderSimple));
    ComPtr<IUnknown> rangeUnknown;
    ASSERT_HRESULT_SUCCEEDED(sliderSimple->GetPatternProvider(UIA_RangeValuePatternId, &rangeUnknown));
    ASSERT_NE(rangeUnknown.Get(), nullptr);
    ComPtr<IRangeValueProvider> range;
    ASSERT_HRESULT_SUCCEEDED(rangeUnknown.As(&range));
    EXPECT_HRESULT_SUCCEEDED(range->SetValue(72.0));
    EXPECT_EQ(range->SetValue(101.0), E_INVALIDARG);
    auto sliderValue = updater.sliderValue(*slider);
    ASSERT_TRUE(sliderValue.has_value());
    EXPECT_FLOAT_EQ(*sliderValue, 72.0F);

    ComPtr<IRawElementProviderFragment> progressFragment;
    ASSERT_HRESULT_SUCCEEDED(sliderFragment->Navigate(NavigateDirection_NextSibling, &progressFragment));
    ComPtr<IRawElementProviderSimple> progressSimple;
    ASSERT_HRESULT_SUCCEEDED(progressFragment.As(&progressSimple));
    ComPtr<IUnknown> progressRangeUnknown;
    ASSERT_HRESULT_SUCCEEDED(progressSimple->GetPatternProvider(UIA_RangeValuePatternId, &progressRangeUnknown));
    ComPtr<IRangeValueProvider> progressRange;
    ASSERT_HRESULT_SUCCEEDED(progressRangeUnknown.As(&progressRange));
    BOOL readOnly = FALSE;
    EXPECT_HRESULT_SUCCEEDED(progressRange->get_IsReadOnly(&readOnly));
    EXPECT_EQ(readOnly, TRUE);
    EXPECT_EQ(progressRange->SetValue(50.0), UIA_E_NOTSUPPORTED);

    ComPtr<IRawElementProviderFragment> textFragment;
    ASSERT_HRESULT_SUCCEEDED(progressFragment->Navigate(NavigateDirection_NextSibling, &textFragment));
    ComPtr<IRawElementProviderSimple> textSimple;
    ASSERT_HRESULT_SUCCEEDED(textFragment.As(&textSimple));
    ComPtr<IUnknown> valueUnknown;
    ASSERT_HRESULT_SUCCEEDED(textSimple->GetPatternProvider(UIA_ValuePatternId, &valueUnknown));
    ASSERT_NE(valueUnknown.Get(), nullptr);
    ComPtr<IValueProvider> value;
    ASSERT_HRESULT_SUCCEEDED(valueUnknown.As(&value));
    BSTR oldValue = nullptr;
    EXPECT_HRESULT_SUCCEEDED(value->get_Value(&oldValue));
    EXPECT_STREQ(oldValue, L"Player");
    ::SysFreeString(oldValue);
    EXPECT_HRESULT_SUCCEEDED(value->SetValue(L"Accessible Player"));
    auto text = updater.text(*textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Accessible Player");

    bridge->detach();
    (void)::DestroyWindow(hwnd);
}

TEST(WindowsUiaHostBridgeTest, FragmentHierarchyAndOldSnapshotSurviveRepublishAndClear)
{
    auto bridgeResult = UI::createWindowsUiaHostBridge();
    ASSERT_TRUE(bridgeResult.has_value());
    auto bridge = std::move(*bridgeResult);
    const HWND hwnd =
        ::CreateWindowExW(0, L"STATIC", L"TinaUiaLifetimeTest", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          320, 200, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    ASSERT_NE(hwnd, nullptr);
    assertOk(bridge->attach(hwnd));

    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    auto modal = updater.createElement(root.rootNodeId(), UI::makeModalElement());
    ASSERT_TRUE(modal.has_value());
    auto button = updater.createElement(*modal, UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(280.0F, 160.0F)));
    assertOk(updater.setLayoutStyle(*modal, fixedSize(240.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(100.0F, 32.0F)));
    assertOk(updater.setText(*button, "Before"));
    assertOk(context->commitLayout({.width = 280.0F, .height = 160.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->committedSemantics()));
    assertOk(bridge->publish(tree, *context));
    ComPtr<IRawElementProviderSimple> rootSimple;
    rootSimple.Attach(bridge->acquireRootProvider());
    ComPtr<IRawElementProviderFragment> rootFragment;
    ASSERT_HRESULT_SUCCEEDED(rootSimple.As(&rootFragment));
    ComPtr<IRawElementProviderFragment> modalFragment;
    ASSERT_HRESULT_SUCCEEDED(rootFragment->Navigate(NavigateDirection_FirstChild, &modalFragment));
    ASSERT_NE(modalFragment.Get(), nullptr);
    ComPtr<IRawElementProviderFragment> oldButtonFragment;
    ASSERT_HRESULT_SUCCEEDED(modalFragment->Navigate(NavigateDirection_FirstChild, &oldButtonFragment));
    ASSERT_NE(oldButtonFragment.Get(), nullptr);
    ComPtr<IRawElementProviderFragment> parentFragment;
    ASSERT_HRESULT_SUCCEEDED(oldButtonFragment->Navigate(NavigateDirection_Parent, &parentFragment));
    ASSERT_NE(parentFragment.Get(), nullptr);

    assertOk(updater.setText(*button, "After"));
    assertOk(context->commitLayout({.width = 280.0F, .height = 160.0F}));
    UI::UIAccessibilityTree fresh;
    assertOk(fresh.rebuildFrom(context->committedSemantics()));
    assertOk(bridge->publish(fresh, *context));
    bridge->clear();

    ComPtr<IRawElementProviderSimple> oldButtonSimple;
    ASSERT_HRESULT_SUCCEEDED(oldButtonFragment.As(&oldButtonSimple));
    VARIANT oldName{};
    ::VariantInit(&oldName);
    ASSERT_HRESULT_SUCCEEDED(oldButtonSimple->GetPropertyValue(UIA_NamePropertyId, &oldName));
    ASSERT_EQ(oldName.vt, VT_BSTR);
    EXPECT_STREQ(oldName.bstrVal, L"Before");
    ::VariantClear(&oldName);
    ComPtr<IUnknown> oldInvokeUnknown;
    ASSERT_HRESULT_SUCCEEDED(oldButtonSimple->GetPatternProvider(UIA_InvokePatternId, &oldInvokeUnknown));
    ComPtr<IInvokeProvider> oldInvoke;
    ASSERT_HRESULT_SUCCEEDED(oldInvokeUnknown.As(&oldInvoke));
    EXPECT_EQ(oldInvoke->Invoke(), UIA_E_ELEMENTNOTAVAILABLE);

    bridge->detach();
    (void)::DestroyWindow(hwnd);
}

TEST(WindowsUiaHostBridgeTest, DetachWithoutAttachIsSafe)
{
    auto bridgeResult = UI::createWindowsUiaHostBridge();
    ASSERT_TRUE(bridgeResult.has_value());
    auto bridge = std::move(*bridgeResult);
    bridge->detach();
    EXPECT_FALSE(bridge->isAttached());
    EXPECT_EQ(bridge->acquireRootProvider(), nullptr);
}

} // namespace
} // namespace Tina::Tests
