#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

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
    capacities.applyDefaultProductChrome = false;
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

TEST(UISemanticsTest, PublishesInteractiveKindsAndOmitsDecorators)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());

    auto panel = context->rootBuilder().createElement(root.rootNodeId(), UI::makePanelElement());
    auto label = context->rootBuilder().createElement(root.rootNodeId(), UI::makeLabelElement());
    auto button = context->rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    auto checkbox = context->rootBuilder().createElement(root.rootNodeId(), UI::makeCheckboxElement());
    auto slider = context->rootBuilder().createElement(root.rootNodeId(), UI::makeSliderElement());
    auto textEdit = context->rootBuilder().createElement(root.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(panel.has_value());
    ASSERT_TRUE(label.has_value());
    ASSERT_TRUE(button.has_value());
    ASSERT_TRUE(checkbox.has_value());
    ASSERT_TRUE(slider.has_value());
    ASSERT_TRUE(textEdit.has_value());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(400.0F, 200.0F)));
    assertOk(updater.setLayoutStyle(*panel, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(*label, fixedSize(80.0F, 24.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 32.0F)));
    assertOk(updater.setLayoutStyle(*checkbox, fixedSize(24.0F, 24.0F)));
    assertOk(updater.setLayoutStyle(*slider, fixedSize(120.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(*textEdit, fixedSize(160.0F, 32.0F)));
    assertOk(updater.setText(*label, "Volume"));
    assertOk(updater.setText(*button, "Apply"));
    assertOk(updater.setChecked(*checkbox, true));
    assertOk(updater.setSliderRange(*slider, 0.0F, 1.0F, 0.1F));
    assertOk(updater.setSliderValue(*slider, 0.4F));
    assertOk(updater.setText(*textEdit, "Player One"));
    assertOk(context->commitLayout({.width = 400.0F, .height = 200.0F}));

    // Cycle the keyboard focus ring to the TextEdit, then republish focused semantics.
    UI::UINodeId focused{};
    for (usize step = 0; step < 4 && focused != *textEdit; ++step) {
        auto focus = context->routeDefaultActionFocusStep(false);
        ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
        EXPECT_TRUE(focus->consumed);
        focused = focus->focus;
    }
    ASSERT_EQ(focused, *textEdit);
    assertOk(context->commitLayout({.width = 400.0F, .height = 200.0F}));

    const auto semantics = context->committedSemantics();
    EXPECT_FALSE(semantics.empty());
    EXPECT_GT(semantics.semanticsRevision(), 0U);

    bool sawLabel = false;
    bool sawButton = false;
    bool sawCheckbox = false;
    bool sawSlider = false;
    bool sawTextEdit = false;
    bool sawPanel = false;
    for (const UI::UISemanticsEntry& entry : semantics.entries()) {
        if (entry.node == *panel) {
            sawPanel = true;
        }
        if (entry.node == *label) {
            sawLabel = true;
            EXPECT_EQ(entry.role, UI::UISemanticsRole::Label);
            EXPECT_EQ(entry.name, "Volume");
        }
        if (entry.node == *button) {
            sawButton = true;
            EXPECT_EQ(entry.role, UI::UISemanticsRole::Button);
            EXPECT_EQ(entry.name, "Apply");
        }
        if (entry.node == *checkbox) {
            sawCheckbox = true;
            EXPECT_EQ(entry.role, UI::UISemanticsRole::Checkbox);
            EXPECT_TRUE(entry.checked);
        }
        if (entry.node == *slider) {
            sawSlider = true;
            EXPECT_EQ(entry.role, UI::UISemanticsRole::Slider);
            EXPECT_TRUE(entry.hasRange);
            EXPECT_FLOAT_EQ(entry.minValue, 0.0F);
            EXPECT_FLOAT_EQ(entry.maxValue, 1.0F);
            EXPECT_FLOAT_EQ(entry.value, 0.4F);
        }
        if (entry.node == *textEdit) {
            sawTextEdit = true;
            EXPECT_EQ(entry.role, UI::UISemanticsRole::TextEdit);
            EXPECT_EQ(entry.valueText, "Player One");
            EXPECT_TRUE(entry.focused);
        }
    }
    EXPECT_TRUE(sawLabel);
    EXPECT_TRUE(sawButton);
    EXPECT_TRUE(sawCheckbox);
    EXPECT_TRUE(sawSlider);
    EXPECT_TRUE(sawTextEdit);
    EXPECT_FALSE(sawPanel);

    const auto stats = context->statistics();
    EXPECT_EQ(stats.committedSemanticsNodeCount, semantics.size());
    EXPECT_EQ(stats.semanticsRevision, semantics.semanticsRevision());
    EXPECT_FALSE(stats.semanticsDirty);
}

TEST(UISemanticsTest, CheckboxToggleAdvancesSemanticsRevision)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = context->rootBuilder().createElement(root.rootNodeId(), UI::makeCheckboxElement());
    ASSERT_TRUE(checkbox.has_value());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*checkbox, fixedSize(40.0F, 40.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const u64 firstRevision = context->committedSemantics().semanticsRevision();

    assertOk(updater.setChecked(*checkbox, true));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const auto semantics = context->committedSemantics();
    EXPECT_GT(semantics.semanticsRevision(), firstRevision);
    ASSERT_EQ(semantics.size(), 1U);
    EXPECT_TRUE(semantics.entries()[0].checked);
}

TEST(UISemanticsTest, PublishedTextRemainsStableUntilTheNextCommit)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto label = context->rootBuilder().createElement(root.rootNodeId(), UI::makeLabelElement());
    auto textEdit = context->rootBuilder().createElement(root.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(label.has_value());
    ASSERT_TRUE(textEdit.has_value());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(*label, fixedSize(100.0F, 24.0F)));
    assertOk(updater.setLayoutStyle(*textEdit, fixedSize(120.0F, 32.0F)));
    assertOk(updater.setText(*label, "Label A"));
    assertOk(updater.setText(*textEdit, "Value A"));
    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));

    const auto published = context->committedSemantics();
    const u64 publishedRevision = published.semanticsRevision();
    ASSERT_EQ(published.size(), 2U);

    assertOk(updater.setText(*label, "Label B"));
    assertOk(updater.setText(*textEdit, "Value B"));
    EXPECT_TRUE(context->statistics().semanticsDirty);

    bool sawOldLabel = false;
    bool sawOldValue = false;
    for (const UI::UISemanticsEntry& entry : published.entries()) {
        if (entry.node == *label) {
            sawOldLabel = true;
            EXPECT_EQ(entry.name, "Label A");
        }
        if (entry.node == *textEdit) {
            sawOldValue = true;
            EXPECT_EQ(entry.valueText, "Value A");
        }
    }
    EXPECT_TRUE(sawOldLabel);
    EXPECT_TRUE(sawOldValue);
    EXPECT_EQ(context->committedSemantics().semanticsRevision(), publishedRevision);

    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));
    const auto updated = context->committedSemantics();
    EXPECT_GT(updated.semanticsRevision(), publishedRevision);
    for (const UI::UISemanticsEntry& entry : updated.entries()) {
        if (entry.node == *label) {
            EXPECT_EQ(entry.name, "Label B");
        }
        if (entry.node == *textEdit) {
            EXPECT_EQ(entry.valueText, "Value B");
        }
    }
}

} // namespace
} // namespace Tina::Tests
