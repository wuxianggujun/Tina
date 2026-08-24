#include "UIInputRouteProducerTestSupport.hpp"

#include <array>
#include <expected>
#include <string_view>
#include <utility>
#include <variant>

namespace Tina::Tests {

UI::UIListViewDataSource ListRouteSource::view() const noexcept
{
    return UI::UIListViewDataSource{
            .state = this,
            .itemCount = [](const void*) noexcept -> u64 { return 4; },
            .resolveItem = [](const void*, u64 logicalIndex, UI::UIListViewItemDescriptor& output) noexcept {
                constexpr std::array<std::string_view, 4> Labels{"Alpha", "Disabled", "Gamma", "Omega"};
                if (logicalIndex >= Labels.size())
                {
                    return false;
                }
                output = UI::UIListViewItemDescriptor{
                    .key = logicalIndex + 1,
                    .label = Labels[logicalIndex],
                    .enabled = logicalIndex != 1,
                };
                return true;
            },
    };
}

UI::UITreeViewDataSource TreeRouteSource::view() noexcept
{
    return UI::UITreeViewDataSource{
            .state = this,
            .itemCount = [](const void* state) noexcept -> u64 {
                return static_cast<const TreeRouteSource*>(state)->rootExpanded ? 4 : 2;
            },
            .resolveItem = [](const void* state, u64 logicalIndex,
                              UI::UITreeViewItemDescriptor& output) noexcept {
                const auto& source = *static_cast<const TreeRouteSource*>(state);
                if (logicalIndex == 0)
                {
                    output = UI::UITreeViewItemDescriptor{
                        .key = 10,
                        .label = "Root",
                        .level = 0,
                        .expandable = true,
                        .expanded = source.rootExpanded,
                    };
                    return true;
                }
                if (!source.rootExpanded)
                {
                    if (logicalIndex != 1)
                    {
                        return false;
                    }
                    output = UI::UITreeViewItemDescriptor{.key = 13, .label = "Sibling", .level = 0};
                    return true;
                }
                switch (logicalIndex)
                {
                case 1:
                    output = UI::UITreeViewItemDescriptor{.key = 11, .label = "Child", .level = 1};
                    return true;
                case 2:
                    output = UI::UITreeViewItemDescriptor{
                        .key = 12,
                        .label = "Disabled child",
                        .level = 1,
                        .enabled = false,
                    };
                    return true;
                case 3:
                    output = UI::UITreeViewItemDescriptor{.key = 13, .label = "Sibling", .level = 0};
                    return true;
                default:
                    return false;
                }
            },
            .setItemExpanded = [](void* state, UI::UITreeViewItemKey key, bool expanded) noexcept {
                if (key != 10)
                {
                    return false;
                }
                static_cast<TreeRouteSource*>(state)->rootExpanded = expanded;
                return true;
            },
    };
}

UI::UIVirtualGridViewDataSource VirtualGridRouteSource::view() const noexcept
{
    return {
        .state = this,
        .itemCount = [](const void*) noexcept -> u64 { return 8; },
        .resolveItem = [](const void*, u64 logicalIndex,
                          UI::UIVirtualGridViewItemDescriptor& output) noexcept {
            constexpr std::array<std::string_view, 8> Labels{
                "A", "Disabled", "C", "D", "E", "F", "G", "H"};
            if (logicalIndex >= Labels.size())
            {
                return false;
            }
            output = UI::UIVirtualGridViewItemDescriptor{
                .key = logicalIndex + 20,
                .label = Labels[logicalIndex],
                .enabled = logicalIndex != 1,
            };
            return true;
        },
    };
}

UI::UIDataGridDataSource DataGridRouteSource::view() const noexcept
{
    return {
        .state = this,
        .rowCount = [](const void*) noexcept -> u64 { return 6; },
        .columnCount = [](const void*) noexcept -> u32 { return 2; },
        .resolveRow = [](const void*, u64 logicalRow,
                         UI::UIDataGridRowDescriptor& output) noexcept {
            if (logicalRow >= 6)
            {
                return false;
            }
            output = UI::UIDataGridRowDescriptor{
                .key = logicalRow + 30,
                .enabled = logicalRow != 1,
            };
            return true;
        },
        .resolveColumn = [](const void*, u32 logicalColumn,
                            UI::UIDataGridColumnDescriptor& output) noexcept {
            constexpr std::array<std::string_view, 2> Headers{"Name", "State"};
            if (logicalColumn >= Headers.size())
            {
                return false;
            }
            output = UI::UIDataGridColumnDescriptor{
                .key = logicalColumn + 40,
                .header = Headers[logicalColumn],
                .width = 50.0F,
            };
            return true;
        },
        .resolveCell = [](const void*, u64 logicalRow, u32 logicalColumn,
                          UI::UIDataGridCellDescriptor& output) noexcept {
            constexpr std::array<std::string_view, 2> Cells{"Asset", "Ready"};
            if (logicalRow >= 6 || logicalColumn >= Cells.size())
            {
                return false;
            }
            output = UI::UIDataGridCellDescriptor{.text = Cells[logicalColumn]};
            return true;
        },
    };
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] Platform::PointerMoveTransition pointerMove(Platform::WindowId window, double x, double y,
                                                          double deltaX, double deltaY) noexcept
{
    return Platform::PointerMoveTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .logicalX = x,
        .logicalY = y,
        .deltaX = deltaX,
        .deltaY = deltaY,
    };
}

[[nodiscard]] Platform::PointerButtonTransition
pointerButton(Platform::WindowId window, Platform::DigitalTransition state, double x, double y) noexcept
{
    return Platform::PointerButtonTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .button = Platform::PointerButton::Primary,
        .state = state,
        .logicalX = x,
        .logicalY = y,
    };
}

[[nodiscard]] Platform::PointerWheelTransition pointerWheel(Platform::WindowId window, double x, double y,
                                                            double deltaX, double deltaY) noexcept
{
    return Platform::PointerWheelTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .deltaX = deltaX,
        .deltaY = deltaY,
        .logicalX = x,
        .logicalY = y,
    };
}

[[nodiscard]] Platform::KeyTransition keyDown(Platform::WindowId window, Platform::Key key) noexcept
{
    return Platform::KeyTransition{
        .window = window,
        .key = key,
        .state = Platform::DigitalTransition::Down,
    };
}

[[nodiscard]] Platform::KeyTransition keyUp(Platform::WindowId window, Platform::Key key) noexcept
{
    return Platform::KeyTransition{
        .window = window,
        .key = key,
        .state = Platform::DigitalTransition::Up,
    };
}

[[nodiscard]] Platform::GamepadButtonTransition
gamepadButtonDown(Platform::WindowId window, Platform::GamepadId gamepad) noexcept
{
    return Platform::GamepadButtonTransition{
        .routedWindow = window,
        .gamepad = gamepad,
        .button = Platform::GamepadButton::South,
        .state = Platform::DigitalTransition::Down,
    };
}

[[nodiscard]] Platform::GamepadButtonTransition
gamepadButtonUp(Platform::WindowId window, Platform::GamepadId gamepad) noexcept
{
    return Platform::GamepadButtonTransition{
        .routedWindow = window,
        .gamepad = gamepad,
        .button = Platform::GamepadButton::South,
        .state = Platform::DigitalTransition::Up,
    };
}

[[nodiscard]] Platform::GamepadButtonTransition gamepadButton(
    Platform::WindowId window, Platform::GamepadId gamepad, Platform::GamepadButton button,
    Platform::DigitalTransition state) noexcept
{
    return Platform::GamepadButtonTransition{
        .routedWindow = window,
        .gamepad = gamepad,
        .button = button,
        .state = state,
    };
}

[[nodiscard]] Platform::GamepadSnapshot
heldSouthSnapshot(Platform::GamepadId gamepad, u64 revision) noexcept
{
    Platform::GamepadSnapshot snapshot{
        .gamepad = gamepad,
        .revision = revision,
    };
    snapshot.heldButtons.set(static_cast<usize>(Platform::GamepadButton::South));
    return snapshot;
}

[[nodiscard]] Platform::GamepadSnapshot
releasedSouthSnapshot(Platform::GamepadId gamepad, u64 revision) noexcept
{
    return Platform::GamepadSnapshot{
        .gamepad = gamepad,
        .revision = revision,
    };
}

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIRoutedPointerListenerToken
addListener(UI::UIContext& context, UI::UIRoutedPointerListenerDesc descriptor, UI::UIRoutedPointerCallback callback)
{
    auto result = context.input().addRoutedPointerListener(descriptor, std::move(callback));
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRoutedPointerListenerToken{};
}

[[nodiscard]] RouteTree createRouteTree(Platform::WindowId window,
                                        UI::UIContextCapacityConfig capacities,
                                        std::pmr::memory_resource& resource)
{
    RouteTree tree;
    auto context = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(context.has_value()) << (context ? "" : context.error().message);
    if (!context)
    {
        return tree;
    }
    tree.context = std::move(*context);

    auto root = tree.context->authoring().rootBuilder().createRoot();
    EXPECT_TRUE(root.has_value()) << (root ? "" : root.error().message);
    if (!root)
    {
        return tree;
    }
    tree.root = std::move(*root);
    auto panel = tree.context->authoring().rootBuilder().createElement(
        tree.root.rootNodeId(), UI::makePanelElement());
    EXPECT_TRUE(panel.has_value()) << (panel ? "" : panel.error().message);
    if (!panel)
    {
        return tree;
    }
    auto target = tree.context->authoring().rootBuilder().createElement(
        *panel, UI::makeButtonElement());
    EXPECT_TRUE(target.has_value()) << (target ? "" : target.error().message);
    if (!target)
    {
        return tree;
    }
    tree.panel = *panel;
    tree.target = *target;

    auto updater = tree.context->authoring().treeUpdater(tree.root);
    EXPECT_TRUE(updater.has_value()) << (updater ? "" : updater.error().message);
    if (!updater)
    {
        return tree;
    }
    tree.updater = std::move(*updater);
    expectOk(tree.updater.setLayoutStyle(tree.root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.panel, fixedSize(80.0F, 80.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.target, fixedSize(40.0F, 40.0F)));
    expectOk(tree.updater.setPointerHitPolicy(tree.target, UI::UIPointerHitPolicy::Targetable));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    return tree;
}

[[nodiscard]] RouteTree createTextEditRouteTree(Platform::WindowId window)
{
    RouteTree tree = createRouteTree(window);
    if (tree.context == nullptr || !tree.target.hasValue())
    {
        return tree;
    }

    const Core::Status destroyButton = tree.updater.destroy(tree.target);
    EXPECT_TRUE(destroyButton.has_value())
        << (destroyButton ? "" : destroyButton.error().message);
    if (!destroyButton)
    {
        return tree;
    }

    auto textEdit = tree.updater.createElement(
        tree.panel, UI::makeTextEditElement());
    EXPECT_TRUE(textEdit.has_value()) << (textEdit ? "" : textEdit.error().message);
    if (!textEdit)
    {
        tree.target = {};
        return tree;
    }
    tree.target = *textEdit;
    expectOk(tree.updater.setLayoutStyle(tree.target, fixedSize(80.0F, 32.0F)));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    return tree;
}

[[nodiscard]] DropdownRouteTree createDropdownRouteTree(Platform::WindowId window)
{
    DropdownRouteTree tree;
    auto context = UI::UIContext::Create(window, {
                                                     .nodeCapacity = 64,
                                                     .rootCapacity = 1,
                                                     .paintSnapshotCapacity = 64,
                                                     .routePathCapacity = 16,
                                                 });
    EXPECT_TRUE(context.has_value()) << (context ? "" : context.error().message);
    if (!context)
    {
        return tree;
    }
    tree.context = std::move(*context);
    auto root = tree.context->authoring().rootBuilder().createRoot();
    EXPECT_TRUE(root.has_value()) << (root ? "" : root.error().message);
    if (!root)
    {
        return tree;
    }
    tree.root = std::move(*root);
    auto updater = tree.context->authoring().treeUpdater(tree.root);
    EXPECT_TRUE(updater.has_value()) << (updater ? "" : updater.error().message);
    if (!updater)
    {
        return tree;
    }
    tree.updater = std::move(*updater);

    auto before = tree.updater.createElement(
        tree.root.rootNodeId(), UI::makeButtonElement());
    auto dropdown = tree.updater.createElement(
        tree.root.rootNodeId(), UI::makeDropdownElement());
    EXPECT_TRUE(before.has_value()) << (before ? "" : before.error().message);
    EXPECT_TRUE(dropdown.has_value()) << (dropdown ? "" : dropdown.error().message);
    if (!before || !dropdown)
    {
        return tree;
    }
    auto popup = tree.updater.createElement(
        *dropdown, UI::makePopupElement());
    EXPECT_TRUE(popup.has_value()) << (popup ? "" : popup.error().message);
    if (!popup)
    {
        return tree;
    }
    auto firstItem = tree.updater.createElement(
        *popup, UI::makeDropdownItemElement());
    auto secondItem = tree.updater.createElement(
        *popup, UI::makeDropdownItemElement());
    auto after = tree.updater.createElement(
        tree.root.rootNodeId(), UI::makeButtonElement());
    EXPECT_TRUE(firstItem.has_value()) << (firstItem ? "" : firstItem.error().message);
    EXPECT_TRUE(secondItem.has_value()) << (secondItem ? "" : secondItem.error().message);
    EXPECT_TRUE(after.has_value()) << (after ? "" : after.error().message);
    if (!firstItem || !secondItem || !after)
    {
        return tree;
    }

    tree.before = *before;
    tree.dropdown = *dropdown;
    tree.popup = *popup;
    tree.firstItem = *firstItem;
    tree.secondItem = *secondItem;
    tree.after = *after;
    UI::UILayoutStyle popupLayout = fixedSize(80.0F, 40.0F);
    popupLayout.placement = UI::UILayoutPlacement::Overlay;
    expectOk(tree.updater.setLayoutStyle(tree.root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.before, fixedSize(80.0F, 20.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.dropdown, fixedSize(80.0F, 24.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.popup, popupLayout));
    expectOk(tree.updater.setLayoutStyle(tree.firstItem, fixedSize(80.0F, 20.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.secondItem, fixedSize(80.0F, 20.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.after, fixedSize(80.0F, 20.0F)));
    expectOk(tree.updater.setDropdownOpen(tree.dropdown, true));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    return tree;
}

[[nodiscard]] MenuRouteTree createMenuRouteTree(Platform::WindowId window)
{
    MenuRouteTree tree;
    auto context = UI::UIContext::Create(window, {
                                                     .nodeCapacity = 32,
                                                     .rootCapacity = 1,
                                                     .paintSnapshotCapacity = 32,
                                                     .routePathCapacity = 16,
                                                 });
    EXPECT_TRUE(context.has_value()) << (context ? "" : context.error().message);
    if (!context)
    {
        return tree;
    }
    tree.context = std::move(*context);
    auto root = tree.context->authoring().rootBuilder().createRoot();
    EXPECT_TRUE(root.has_value()) << (root ? "" : root.error().message);
    if (!root)
    {
        return tree;
    }
    tree.root = std::move(*root);
    auto updater = tree.context->authoring().treeUpdater(tree.root);
    EXPECT_TRUE(updater.has_value()) << (updater ? "" : updater.error().message);
    if (!updater)
    {
        return tree;
    }
    tree.updater = std::move(*updater);

    auto anchor = tree.updater.createElement(
        tree.root.rootNodeId(), UI::makeButtonElement("Menu", fixedSize(80.0F, 24.0F)));
    auto menu = tree.updater.createElement(
        tree.root.rootNodeId(),
        UI::makeMenuElement(
            {.placement = UI::UIMenuPlacement::Below}, fixedSize(80.0F, 48.0F)));
    EXPECT_TRUE(anchor.has_value()) << (anchor ? "" : anchor.error().message);
    EXPECT_TRUE(menu.has_value()) << (menu ? "" : menu.error().message);
    if (!anchor || !menu)
    {
        return tree;
    }
    auto firstItem = tree.updater.createElement(
        *menu, UI::makeMenuItemElement("First", {}, fixedSize(72.0F, 20.0F)));
    auto secondItem = tree.updater.createElement(
        *menu, UI::makeMenuItemElement(
                   "Second", {.kind = UI::UIMenuItemKind::Check},
                   fixedSize(72.0F, 20.0F)));
    auto after = tree.updater.createElement(
        tree.root.rootNodeId(), UI::makeButtonElement("After", fixedSize(80.0F, 24.0F)));
    EXPECT_TRUE(firstItem.has_value()) << (firstItem ? "" : firstItem.error().message);
    EXPECT_TRUE(secondItem.has_value()) << (secondItem ? "" : secondItem.error().message);
    EXPECT_TRUE(after.has_value()) << (after ? "" : after.error().message);
    if (!firstItem || !secondItem || !after)
    {
        return tree;
    }

    tree.anchor = *anchor;
    tree.menu = *menu;
    tree.firstItem = *firstItem;
    tree.secondItem = *secondItem;
    tree.after = *after;
    expectOk(tree.updater.setLayoutStyle(tree.root.rootNodeId(), fixedSize(120.0F, 120.0F)));
    expectOk(tree.updater.setMenuAnchor(tree.menu, tree.anchor));
    expectOk(tree.context->publication().commitLayout({.width = 120.0F, .height = 120.0F}));
    expectOk(tree.context->input().requestFocus(tree.anchor));
    expectOk(tree.updater.setMenuOpen(tree.menu, true));
    expectOk(tree.context->publication().commitLayout({.width = 120.0F, .height = 120.0F}));
    return tree;
}

[[nodiscard]] CollectionRouteTree createCollectionRouteTree(Platform::WindowId window)
{
    CollectionRouteTree tree;
    auto context = UI::UIContext::Create(window, {
                                                     .nodeCapacity = 192,
                                                     .rootCapacity = 1,
                                                     .paintSnapshotCapacity = 256,
                                                     .routePathCapacity = 16,
                                                 });
    EXPECT_TRUE(context.has_value()) << (context ? "" : context.error().message);
    if (!context)
    {
        return tree;
    }
    tree.context = std::move(*context);
    auto root = tree.context->authoring().rootBuilder().createRoot();
    EXPECT_TRUE(root.has_value()) << (root ? "" : root.error().message);
    if (!root)
    {
        return tree;
    }
    tree.root = std::move(*root);
    auto updater = tree.context->authoring().treeUpdater(tree.root);
    EXPECT_TRUE(updater.has_value()) << (updater ? "" : updater.error().message);
    if (!updater)
    {
        return tree;
    }
    tree.updater = std::move(*updater);
    tree.listSource = std::make_unique<ListRouteSource>();
    tree.treeSource = std::make_unique<TreeRouteSource>();
    tree.virtualGridSource = std::make_unique<VirtualGridRouteSource>();
    tree.dataGridSource = std::make_unique<DataGridRouteSource>();

    auto listView = tree.updater.createElement(
        tree.root.rootNodeId(),
        UI::makeListViewElement({.materializedItemCapacity = 6}));
    auto treeView = tree.updater.createElement(
        tree.root.rootNodeId(),
        UI::makeTreeViewElement({.materializedItemCapacity = 6}));
    auto virtualGridView = tree.updater.createElement(
        tree.root.rootNodeId(),
        UI::makeVirtualGridViewElement({.materializedItemCapacity = 6}));
    auto dataGrid = tree.updater.createElement(
        tree.root.rootNodeId(),
        UI::makeDataGridElement({
            .columnCapacity = 2,
            .materializedRowCapacity = 5,
        }));
    auto other = tree.updater.createElement(
        tree.root.rootNodeId(), UI::makeButtonElement());
    EXPECT_TRUE(listView.has_value()) << (listView ? "" : listView.error().message);
    EXPECT_TRUE(treeView.has_value()) << (treeView ? "" : treeView.error().message);
    EXPECT_TRUE(virtualGridView.has_value())
        << (virtualGridView ? "" : virtualGridView.error().message);
    EXPECT_TRUE(dataGrid.has_value())
        << (dataGrid ? "" : dataGrid.error().message);
    EXPECT_TRUE(other.has_value()) << (other ? "" : other.error().message);
    if (!listView || !treeView || !virtualGridView || !dataGrid || !other)
    {
        return tree;
    }
    tree.listView = *listView;
    tree.treeView = *treeView;
    tree.virtualGridView = *virtualGridView;
    tree.dataGrid = *dataGrid;
    tree.other = *other;

    expectOk(tree.updater.setLayoutStyle(tree.root.rootNodeId(), fixedSize(100.0F, 200.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.listView, fixedSize(100.0F, 40.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.treeView, fixedSize(100.0F, 40.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.virtualGridView, fixedSize(100.0F, 40.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.dataGrid, fixedSize(100.0F, 60.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.other, fixedSize(100.0F, 20.0F)));
    expectOk(tree.updater.setListViewStyle(tree.listView, {.rowHeight = 24.0F, .overscanRows = 1}));
    expectOk(tree.updater.setTreeViewStyle(tree.treeView, {.rowHeight = 24.0F, .overscanRows = 1}));
    expectOk(tree.updater.setVirtualGridViewStyle(
        tree.virtualGridView,
        {
            .minimumItemWidth = 50.0F,
            .itemHeight = 20.0F,
            .columnGap = 0.0F,
            .rowGap = 0.0F,
            .maximumColumnCount = 2,
            .overscanRows = 1,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        }));
    expectOk(tree.updater.setDataGridStyle(
        tree.dataGrid,
        {
            .columnHeaderHeight = 15.0F,
            .rowHeight = 15.0F,
            .overscanRows = 1,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        }));
    expectOk(tree.updater.setListViewDataSource(tree.listView, tree.listSource->view()));
    expectOk(tree.updater.setTreeViewDataSource(tree.treeView, tree.treeSource->view()));
    expectOk(tree.updater.setVirtualGridViewDataSource(
        tree.virtualGridView, tree.virtualGridSource->view()));
    expectOk(tree.updater.setDataGridDataSource(
        tree.dataGrid, tree.dataGridSource->view()));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 200.0F}));
    return tree;
}

[[nodiscard]] Core::Result<Platform::PlatformFrameView> buildFrame(Platform::PlatformFrameBuilder& builder,
                                                                   Platform::WindowId window, const FrameSpec& spec)
{
    if (auto status = builder.beginFrame(spec.frameId); !status)
    {
        return std::unexpected(std::move(status.error()));
    }

    const Platform::WindowMetricsSnapshot metrics{
        .window = window,
        .logicalExtent = {100, 100},
        .framebufferExtent = {100, 100},
        .contentScale = {1.0F, 1.0F},
        .revision = spec.frameId.value,
        .focused = true,
        .visible = true,
    };
    Platform::WindowInputSnapshot input{
        .window = window,
        .sourceMetricsRevision = spec.frameId.value,
    };
    input.pointer.logicalX = spec.pointerX;
    input.pointer.logicalY = spec.pointerY;
    input.pointer.accumulatedDeltaX = spec.accumulatedDeltaX;
    input.pointer.accumulatedDeltaY = spec.accumulatedDeltaY;
    for (Platform::Key key : spec.heldKeys)
    {
        input.heldKeys.set(static_cast<usize>(key));
    }
    for (Platform::PointerButton button : spec.heldPointerButtons)
    {
        input.pointer.heldButtons.set(static_cast<usize>(button));
    }
    if (!builder.setPrimaryWindowSnapshot(metrics, input) ||
        !builder.setGamepadSnapshots(spec.gamepadSnapshots))
    {
        return Core::failure(Core::CoreErrorCode::Internal, "test frame snapshot was rejected");
    }
    for (const Platform::InputTransitionPayload& transition : spec.transitions)
    {
        const Platform::FrameBatchAppendResult result = builder.appendInputTransition(transition);
        if (result != Platform::FrameBatchAppendResult::Appended &&
            result != Platform::FrameBatchAppendResult::Coalesced &&
            result != Platform::FrameBatchAppendResult::ResetInserted)
        {
            return Core::failure(Core::CoreErrorCode::Internal, "test transition append failed");
        }
    }
    for (const Platform::PlatformEventPayload& event : spec.platformEvents)
    {
        const Platform::FrameBatchAppendResult result = builder.appendPlatformEvent(event);
        if (result != Platform::FrameBatchAppendResult::Appended &&
            result != Platform::FrameBatchAppendResult::Coalesced &&
            result != Platform::FrameBatchAppendResult::ResetInserted)
        {
            return Core::failure(Core::CoreErrorCode::Internal, "test platform event append failed");
        }
    }
    return builder.finishFrame();
}

[[nodiscard]] std::unique_ptr<UIInputRouteProducer>
createProducer(usize rawTransitionCapacity, usize continuousControlClaimCapacity,
               std::pmr::memory_resource& resource)
{
    auto producer =
        UIInputRouteProducer::Create(rawTransitionCapacity, continuousControlClaimCapacity, resource);
    EXPECT_TRUE(producer.has_value()) << (producer ? "" : producer.error().message);
    return producer ? std::move(*producer) : nullptr;
}

[[nodiscard]] std::unique_ptr<ActionMapper> createPointerMapper()
{
    const std::array bindings{
        InputActionBinding{
            .input =
                PrimaryPointerButtonBinding{
                    .pointer = Platform::PrimaryPointerId,
                    .button = Platform::PointerButton::Primary,
                },
            .action = PointerAction,
            .domain = InputActionDomain::Simulation,
        },
    };
    auto mapper = ActionMapper::Create(InputActionMapConfig{
        .bindings = std::vector<InputActionBinding>(bindings.begin(), bindings.end()),
    });
    EXPECT_TRUE(mapper.has_value()) << (mapper ? "" : mapper.error().message);
    return mapper ? std::move(*mapper) : nullptr;
}

[[nodiscard]] std::unique_ptr<ActionMapper> createKeyMapper(Platform::Key key)
{
    const std::array bindings{
        InputActionBinding{
            .input = PrimaryWindowKeyBinding{.key = key},
            .action = NavigationAction,
            .domain = InputActionDomain::Simulation,
        },
    };
    auto mapper = ActionMapper::Create(InputActionMapConfig{
        .bindings = std::vector<InputActionBinding>(bindings.begin(), bindings.end()),
    });
    EXPECT_TRUE(mapper.has_value()) << (mapper ? "" : mapper.error().message);
    return mapper ? std::move(*mapper) : nullptr;
}

[[nodiscard]] const InputActionTransition* digital(const SimulationActionTransition& transition)
{
    return std::get_if<InputActionTransition>(&transition);
}

void UIInputRouteProducerTest::SetUp()
{
    auto poolResult = WindowPool::Create(2);
    ASSERT_TRUE(poolResult.has_value());
    windows = std::make_unique<WindowPool>(std::move(*poolResult));
    auto first = windows->tryEmplace(1);
    auto second = windows->tryEmplace(2);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    window = *first;
    otherWindow = *second;

    auto gamepadPoolResult = GamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    gamepads = std::make_unique<GamepadPool>(std::move(*gamepadPoolResult));
    auto gamepadResult = gamepads->tryEmplace(1);
    ASSERT_TRUE(gamepadResult.has_value());
    gamepad = *gamepadResult;

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 128,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value()) << (builderResult ? "" : builderResult.error().message);
    builder = std::make_unique<Platform::PlatformFrameBuilder>(std::move(*builderResult));

    auto cameraBuilderResult = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(cameraBuilderResult.has_value())
        << (cameraBuilderResult ? "" : cameraBuilderResult.error().message);
    auto cameraBuilder = std::move(*cameraBuilderResult);
    ASSERT_TRUE(cameraBuilder.beginFrame().has_value());
    ASSERT_TRUE(cameraBuilder.writer()
                    .setCamera2D(Render::RenderCamera2DInput{
                        .stableCameraKey = 1,
                        .worldWidth = 100.0F,
                        .worldHeight = 100.0F,
                        .actualPixelsPerMeter = 1.0F,
                    })
                    .has_value());
    auto cameraScene = cameraBuilder.commit();
    ASSERT_TRUE(cameraScene.has_value()) << (cameraScene ? "" : cameraScene.error().message);
    lastPresentedCamera2D.notePresented(*cameraScene, 1);
}

} // namespace Tina::Tests
