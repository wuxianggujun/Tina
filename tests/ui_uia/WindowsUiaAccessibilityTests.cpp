#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>
#include <tina/ui/WindowsUiaAccessibilityProviderFactory.hpp>

#include "WindowsUiaAccessibilityProvider.hpp"
#include "WindowsUiaHostBridge.hpp"
#include "UIUiaMapping.hpp"

#include <UIAutomation.h>
#include <Windows.h>

#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 32,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 32,
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
    EXPECT_EQ(UI::Uia::controlTypeFromRole(UI::UISemanticsRole::Group), UI::Uia::kControlTypeGroup);
}

TEST(WindowsUiaMappingTest, MapsEnabledFocusRangeToggleAndValue)
{
    UI::UIAccessibilityNode button{
        .role = UI::UISemanticsRole::Button,
        .kind = UI::UIWidgetKind::Button,
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
        .kind = UI::UIWidgetKind::Checkbox,
        .name = "Music",
        .states = UI::UIAccessibilityState::Enabled | UI::UIAccessibilityState::Checked,
    };
    const auto mappedCheckbox = UI::Uia::mapAccessibilityNode(checkbox);
    ASSERT_TRUE(mappedCheckbox.toggleState.has_value());
    EXPECT_EQ(*mappedCheckbox.toggleState, UI::Uia::kToggleStateOn);

    UI::UIAccessibilityNode slider{
        .role = UI::UISemanticsRole::Slider,
        .kind = UI::UIWidgetKind::Slider,
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
        .kind = UI::UIWidgetKind::ProgressBar,
        .value = 55.0F,
        .minValue = 0.0F,
        .maxValue = 100.0F,
        .states = UI::UIAccessibilityState::Enabled | UI::UIAccessibilityState::HasRange
            | UI::UIAccessibilityState::ReadOnly,
    };
    const auto mappedProgress = UI::Uia::mapAccessibilityNode(progress);
    ASSERT_TRUE(mappedProgress.rangeValue.has_value());
    EXPECT_TRUE(mappedProgress.rangeValue->isReadOnly);
    EXPECT_FALSE(mappedProgress.isKeyboardFocusable);

    UI::UIAccessibilityNode textEdit{
        .role = UI::UISemanticsRole::TextEdit,
        .kind = UI::UIWidgetKind::TextEdit,
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
        .kind = UI::UIWidgetKind::Button,
        .name = "Off",
        .states = UI::UIAccessibilityState::Disabled | UI::UIAccessibilityState::Focused,
    };
    const auto mapped = UI::Uia::mapAccessibilityNode(button);
    EXPECT_FALSE(mapped.isEnabled);
    EXPECT_FALSE(mapped.isKeyboardFocusable);
    EXPECT_FALSE(mapped.hasKeyboardFocus);
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

    auto button = context->rootBuilder().createButton(root.rootNodeId());
    auto checkbox = context->rootBuilder().createCheckbox(root.rootNodeId());
    auto slider = context->rootBuilder().createSlider(root.rootNodeId());
    auto progress = context->rootBuilder().createProgressBar(root.rootNodeId());
    auto radio = context->rootBuilder().createRadioButton(root.rootNodeId());
    auto textEdit = context->rootBuilder().createTextEdit(root.rootNodeId());
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

    auto button = context->rootBuilder().createButton(root.rootNodeId());
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

    const HWND hwnd = ::CreateWindowExW(
        0, L"STATIC", L"TinaUiaBridgeTest", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 200, nullptr,
        nullptr, ::GetModuleHandleW(nullptr), nullptr);
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
    auto button = context->rootBuilder().createButton(root.rootNodeId());
    ASSERT_TRUE(button.has_value());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 32.0F)));
    assertOk(updater.setText(*button, "Bridge"));
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->committedSemantics()));
    assertOk(bridge->publish(tree));
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
    if (firstChild != nullptr) {
        IRawElementProviderSimple* childSimple = nullptr;
        ASSERT_HRESULT_SUCCEEDED(firstChild->QueryInterface(IID_PPV_ARGS(&childSimple)));
        ASSERT_NE(childSimple, nullptr);
        VARIANT controlType{};
        ::VariantInit(&controlType);
        ASSERT_HRESULT_SUCCEEDED(childSimple->GetPropertyValue(UIA_ControlTypePropertyId, &controlType));
        EXPECT_EQ(controlType.vt, VT_I4);
        ::VariantClear(&controlType);
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
