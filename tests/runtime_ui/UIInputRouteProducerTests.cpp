#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UI.hpp>

#include "../../src/runtime/input/ActionMapper.hpp"
#include "../../src/runtime/input/LastPresentedCamera2DLatch.hpp"
#include "../../src/runtime/input/UIInputRouteProducer.hpp"

#include <array>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::Tests {
namespace {

using Runtime::Input::ActionMapper;
using Runtime::Input::LastPresentedCamera2DLatch;
using Runtime::Input::UIInputRouteProducer;
using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;
using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

inline constexpr InputActionId PointerAction{41};

struct FrameSpec final {
    Platform::PlatformFrameId frameId{1};
    std::vector<Platform::InputTransitionPayload> transitions{};
    std::vector<Platform::Key> heldKeys{};
    std::vector<Platform::PointerButton> heldPointerButtons{};
    double pointerX = 10.0;
    double pointerY = 10.0;
    double accumulatedDeltaX = 0.0;
    double accumulatedDeltaY = 0.0;
    std::vector<Platform::GamepadSnapshot> gamepadSnapshots{};
};

struct RouteTree final {
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
    UI::UINodeId panel{};
    UI::UINodeId target{};
};

struct DropdownRouteTree final {
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
    UI::UINodeId before{};
    UI::UINodeId dropdown{};
    UI::UINodeId popup{};
    UI::UINodeId firstItem{};
    UI::UINodeId secondItem{};
    UI::UINodeId after{};
};

struct PointerEventTrace final {
    std::array<UI::UIPointerInputEvent, 8> events{};
    usize size = 0;

    void push(const UI::UIPointerInputEvent& event) noexcept
    {
        if (size < events.size())
        {
            events[size] = event;
            ++size;
        }
    }
};

class ObservingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return allocationCount_;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++allocationCount_;
        return upstream_->allocate(bytes, alignment);
    }

    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override
    {
        upstream_->deallocate(memory, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_ = std::pmr::new_delete_resource();
    usize allocationCount_ = 0;
};

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] Platform::PointerMoveTransition pointerMove(Platform::WindowId window, double x, double y,
                                                          double deltaX = 0.0, double deltaY = 0.0) noexcept
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
    auto result = context.addRoutedPointerListener(descriptor, std::move(callback));
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRoutedPointerListenerToken{};
}

[[nodiscard]] RouteTree createRouteTree(Platform::WindowId window,
                                        UI::UIContextCapacityConfig capacities =
                                            {
                                                .nodeCapacity = 8,
                                                .rootCapacity = 1,
                                                .routePathCapacity = 8,
                                                .routedPointerListenerCapacity = 16,
                                            },
                                        std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    RouteTree tree;
    auto context = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(context.has_value()) << (context ? "" : context.error().message);
    if (!context)
    {
        return tree;
    }
    tree.context = std::move(*context);

    auto root = tree.context->rootBuilder().createRoot();
    EXPECT_TRUE(root.has_value()) << (root ? "" : root.error().message);
    if (!root)
    {
        return tree;
    }
    tree.root = std::move(*root);
    auto panel = tree.context->rootBuilder().createPanel(tree.root.rootNodeId());
    EXPECT_TRUE(panel.has_value()) << (panel ? "" : panel.error().message);
    if (!panel)
    {
        return tree;
    }
    auto target = tree.context->rootBuilder().createButton(*panel);
    EXPECT_TRUE(target.has_value()) << (target ? "" : target.error().message);
    if (!target)
    {
        return tree;
    }
    tree.panel = *panel;
    tree.target = *target;

    auto updater = tree.context->treeUpdater(tree.root);
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
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
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

    auto textEdit = tree.updater.createTextEdit(tree.panel);
    EXPECT_TRUE(textEdit.has_value()) << (textEdit ? "" : textEdit.error().message);
    if (!textEdit)
    {
        tree.target = {};
        return tree;
    }
    tree.target = *textEdit;
    expectOk(tree.updater.setLayoutStyle(tree.target, fixedSize(80.0F, 32.0F)));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
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
    auto root = tree.context->rootBuilder().createRoot();
    EXPECT_TRUE(root.has_value()) << (root ? "" : root.error().message);
    if (!root)
    {
        return tree;
    }
    tree.root = std::move(*root);
    auto updater = tree.context->treeUpdater(tree.root);
    EXPECT_TRUE(updater.has_value()) << (updater ? "" : updater.error().message);
    if (!updater)
    {
        return tree;
    }
    tree.updater = std::move(*updater);

    auto before = tree.updater.createButton(tree.root.rootNodeId());
    auto dropdown = tree.updater.createDropdown(tree.root.rootNodeId());
    EXPECT_TRUE(before.has_value()) << (before ? "" : before.error().message);
    EXPECT_TRUE(dropdown.has_value()) << (dropdown ? "" : dropdown.error().message);
    if (!before || !dropdown)
    {
        return tree;
    }
    auto popup = tree.updater.createPopup(*dropdown);
    EXPECT_TRUE(popup.has_value()) << (popup ? "" : popup.error().message);
    if (!popup)
    {
        return tree;
    }
    auto firstItem = tree.updater.createDropdownItem(*popup);
    auto secondItem = tree.updater.createDropdownItem(*popup);
    auto after = tree.updater.createButton(tree.root.rootNodeId());
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
    popupLayout.position = UI::UILayoutPositionMode::AbsoluteOverlay;
    expectOk(tree.updater.setLayoutStyle(tree.root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.before, fixedSize(80.0F, 20.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.dropdown, fixedSize(80.0F, 24.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.popup, popupLayout));
    expectOk(tree.updater.setLayoutStyle(tree.firstItem, fixedSize(80.0F, 20.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.secondItem, fixedSize(80.0F, 20.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.after, fixedSize(80.0F, 20.0F)));
    expectOk(tree.updater.setDropdownOpen(tree.dropdown, true));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
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
    return builder.finishFrame();
}

[[nodiscard]] std::unique_ptr<UIInputRouteProducer>
createProducer(usize rawTransitionCapacity = 128,
               usize continuousControlClaimCapacity =
                   InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity,
               std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
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

[[nodiscard]] const InputActionTransition* digital(const SimulationActionTransition& transition)
{
    return std::get_if<InputActionTransition>(&transition);
}

class UIInputRouteProducerTest : public testing::Test {
  protected:
    void SetUp() override
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

    std::unique_ptr<WindowPool> windows;
    std::unique_ptr<GamepadPool> gamepads;
    std::unique_ptr<Platform::PlatformFrameBuilder> builder;
    LastPresentedCamera2DLatch lastPresentedCamera2D;
    Platform::WindowId window{};
    Platform::WindowId otherWindow{};
    Platform::GamepadId gamepad{};
};

TEST_F(UIInputRouteProducerTest, NullContextProducesNoneViews)
{
    auto producer = createProducer();
    ASSERT_NE(producer, nullptr);
    auto frame = buildFrame(*builder, window,
                            {
                                .frameId = {1},
                                .transitions = {pointerMove(window, 10.0, 10.0)},
                                .pointerX = 10.0,
                                .pointerY = 10.0,
                            });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    auto output = producer->produce(nullptr, *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_EQ(output->consumption.platformFrame, frame->id());
    EXPECT_EQ(output->consumption.transitionCount, frame->inputTransitions().size());
    EXPECT_TRUE(output->consumption.consumedOrdinalWords.empty());
    EXPECT_EQ(output->claims.platformFrame, frame->id());
    EXPECT_TRUE(output->claims.controls.empty());
}

TEST_F(UIInputRouteProducerTest, FocusedTextEditConsumesTabCommandsTextAndAcceptKeys)
{
    constexpr std::string_view InitialUtf8 = "A" "\xE4\xBD\xA0" "B";
    auto producer = createProducer();
    RouteTree tree = createTextEditRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_TRUE(tree.target.hasValue());
    expectOk(tree.updater.setText(tree.target, InitialUtf8));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto tabFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {10},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(tabFrame.has_value()) << (tabFrame ? "" : tabFrame.error().message);
    auto tabOutput = producer->produce(tree.context.get(), *tabFrame);
    ASSERT_TRUE(tabOutput.has_value()) << (tabOutput ? "" : tabOutput.error().message);
    EXPECT_TRUE(tabOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.target);
    EXPECT_EQ(tree.context->imeFocus(), tree.target);

    auto compositionFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {11},
            .transitions = {Platform::TextCompositionTransition{
                .window = window,
                .preeditUtf8 = "ni",
                .cursorCodepoint = 1,
                .stage = Platform::TextCompositionStage::Started,
            }},
        });
    ASSERT_TRUE(compositionFrame.has_value())
        << (compositionFrame ? "" : compositionFrame.error().message);
    auto compositionOutput = producer->produce(tree.context.get(), *compositionFrame);
    ASSERT_TRUE(compositionOutput.has_value())
        << (compositionOutput ? "" : compositionOutput.error().message);
    EXPECT_TRUE(compositionOutput->consumption.isConsumed(0));
    EXPECT_TRUE(tree.context->imeCompositionActive());
    EXPECT_EQ(tree.context->imePreeditUtf8(), "ni");

    auto selectAllFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {12},
            .transitions = {keyDown(window, Platform::Key::A)},
            .heldKeys = {Platform::Key::A, Platform::Key::LeftControl},
        });
    ASSERT_TRUE(selectAllFrame.has_value())
        << (selectAllFrame ? "" : selectAllFrame.error().message);
    auto selectAllOutput = producer->produce(tree.context.get(), *selectAllFrame);
    ASSERT_TRUE(selectAllOutput.has_value())
        << (selectAllOutput ? "" : selectAllOutput.error().message);
    EXPECT_TRUE(selectAllOutput->consumption.isConsumed(0));
    auto selection = tree.updater.textSelection(tree.target);
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 0, .caretCodepoint = 3}));
    EXPECT_FALSE(tree.context->imeCompositionActive());

    auto textFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {13},
            .transitions = {Platform::TextInputTransition{
                .window = window,
                .committedUtf8 = "Z",
            }},
        });
    ASSERT_TRUE(textFrame.has_value()) << (textFrame ? "" : textFrame.error().message);
    auto textOutput = producer->produce(tree.context.get(), *textFrame);
    ASSERT_TRUE(textOutput.has_value()) << (textOutput ? "" : textOutput.error().message);
    EXPECT_TRUE(textOutput->consumption.isConsumed(0));
    auto text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value()) << (text ? "" : text.error().message);
    EXPECT_EQ(*text, "Z");

    auto backspaceFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {14},
            .transitions = {keyDown(window, Platform::Key::Backspace)},
            .heldKeys = {Platform::Key::Backspace},
        });
    ASSERT_TRUE(backspaceFrame.has_value())
        << (backspaceFrame ? "" : backspaceFrame.error().message);
    auto backspaceOutput = producer->produce(tree.context.get(), *backspaceFrame);
    ASSERT_TRUE(backspaceOutput.has_value())
        << (backspaceOutput ? "" : backspaceOutput.error().message);
    EXPECT_TRUE(backspaceOutput->consumption.isConsumed(0));
    text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_TRUE(text->empty());

    auto acceptFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {15},
            .transitions = {
                keyDown(window, Platform::Key::Enter),
                keyDown(window, Platform::Key::Space),
            },
            .heldKeys = {Platform::Key::Enter, Platform::Key::Space},
        });
    ASSERT_TRUE(acceptFrame.has_value()) << (acceptFrame ? "" : acceptFrame.error().message);
    auto acceptOutput = producer->produce(tree.context.get(), *acceptFrame);
    ASSERT_TRUE(acceptOutput.has_value())
        << (acceptOutput ? "" : acceptOutput.error().message);
    EXPECT_TRUE(acceptOutput->consumption.isConsumed(0));
    EXPECT_TRUE(acceptOutput->consumption.isConsumed(1));
    text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_TRUE(text->empty());
}

TEST_F(UIInputRouteProducerTest, DropdownConsumesArrowEscapeAndTabDownUpPairs)
{
    auto producer = createProducer();
    DropdownRouteTree tree = createDropdownRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_TRUE(tree.dropdown.hasValue());
    ASSERT_EQ(tree.context->activePopup(), tree.popup);

    auto down = buildFrame(*builder, window,
                           {
                               .frameId = {60},
                               .transitions = {keyDown(window, Platform::Key::Down)},
                               .heldKeys = {Platform::Key::Down},
                           });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.firstItem);

    auto downRelease = buildFrame(*builder, window,
                                  {
                                      .frameId = {61},
                                      .transitions = {keyUp(window, Platform::Key::Down)},
                                  });
    ASSERT_TRUE(downRelease.has_value()) << (downRelease ? "" : downRelease.error().message);
    auto downReleaseOutput = producer->produce(tree.context.get(), *downRelease);
    ASSERT_TRUE(downReleaseOutput.has_value())
        << (downReleaseOutput ? "" : downReleaseOutput.error().message);
    EXPECT_TRUE(downReleaseOutput->consumption.isConsumed(0));

    auto up = buildFrame(*builder, window,
                         {
                             .frameId = {62},
                             .transitions = {keyDown(window, Platform::Key::Up)},
                             .heldKeys = {Platform::Key::Up},
                         });
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    auto upOutput = producer->produce(tree.context.get(), *up);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    EXPECT_TRUE(upOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.firstItem);

    auto upRelease = buildFrame(*builder, window,
                                {
                                    .frameId = {63},
                                    .transitions = {keyUp(window, Platform::Key::Up)},
                                });
    ASSERT_TRUE(upRelease.has_value()) << (upRelease ? "" : upRelease.error().message);
    auto upReleaseOutput = producer->produce(tree.context.get(), *upRelease);
    ASSERT_TRUE(upReleaseOutput.has_value()) << (upReleaseOutput ? "" : upReleaseOutput.error().message);
    EXPECT_TRUE(upReleaseOutput->consumption.isConsumed(0));

    auto tab = buildFrame(*builder, window,
                          {
                              .frameId = {64},
                              .transitions = {keyDown(window, Platform::Key::Tab)},
                              .heldKeys = {Platform::Key::Tab},
                          });
    ASSERT_TRUE(tab.has_value()) << (tab ? "" : tab.error().message);
    auto tabOutput = producer->produce(tree.context.get(), *tab);
    ASSERT_TRUE(tabOutput.has_value()) << (tabOutput ? "" : tabOutput.error().message);
    EXPECT_TRUE(tabOutput->consumption.isConsumed(0));
    EXPECT_FALSE(tree.context->activePopup().hasValue());
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.after);

    auto tabRelease = buildFrame(*builder, window,
                                 {
                                     .frameId = {65},
                                     .transitions = {keyUp(window, Platform::Key::Tab)},
                                 });
    ASSERT_TRUE(tabRelease.has_value()) << (tabRelease ? "" : tabRelease.error().message);
    auto tabReleaseOutput = producer->produce(tree.context.get(), *tabRelease);
    ASSERT_TRUE(tabReleaseOutput.has_value()) << (tabReleaseOutput ? "" : tabReleaseOutput.error().message);
    EXPECT_TRUE(tabReleaseOutput->consumption.isConsumed(0));

    expectOk(tree.updater.setDropdownOpen(tree.dropdown, true));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    auto escape = buildFrame(*builder, window,
                             {
                                 .frameId = {66},
                                 .transitions = {keyDown(window, Platform::Key::Escape)},
                                 .heldKeys = {Platform::Key::Escape},
                             });
    ASSERT_TRUE(escape.has_value()) << (escape ? "" : escape.error().message);
    auto escapeOutput = producer->produce(tree.context.get(), *escape);
    ASSERT_TRUE(escapeOutput.has_value()) << (escapeOutput ? "" : escapeOutput.error().message);
    EXPECT_TRUE(escapeOutput->consumption.isConsumed(0));
    EXPECT_FALSE(tree.context->activePopup().hasValue());

    auto escapeRelease = buildFrame(*builder, window,
                                    {
                                        .frameId = {67},
                                        .transitions = {keyUp(window, Platform::Key::Escape)},
                                    });
    ASSERT_TRUE(escapeRelease.has_value()) << (escapeRelease ? "" : escapeRelease.error().message);
    auto escapeReleaseOutput = producer->produce(tree.context.get(), *escapeRelease);
    ASSERT_TRUE(escapeReleaseOutput.has_value())
        << (escapeReleaseOutput ? "" : escapeReleaseOutput.error().message);
    EXPECT_TRUE(escapeReleaseOutput->consumption.isConsumed(0));
}

TEST_F(UIInputRouteProducerTest, DropdownConsumesShiftTabAndGamepadNavigationWithoutStealingClosedKeys)
{
    auto producer = createProducer();
    DropdownRouteTree tree = createDropdownRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto shiftTab = buildFrame(*builder, window,
                               {
                                   .frameId = {80},
                                   .transitions = {keyDown(window, Platform::Key::Tab)},
                                   .heldKeys = {Platform::Key::Tab, Platform::Key::LeftShift},
                               });
    ASSERT_TRUE(shiftTab.has_value()) << (shiftTab ? "" : shiftTab.error().message);
    auto shiftTabOutput = producer->produce(tree.context.get(), *shiftTab);
    ASSERT_TRUE(shiftTabOutput.has_value()) << (shiftTabOutput ? "" : shiftTabOutput.error().message);
    EXPECT_TRUE(shiftTabOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.before);
    EXPECT_FALSE(tree.context->activePopup().hasValue());

    // Shift is already released in this snapshot. Tab Up must still release the
    // ExitPrevious command selected by the original key-down.
    auto shiftTabRelease = buildFrame(*builder, window,
                                      {
                                          .frameId = {81},
                                          .transitions = {keyUp(window, Platform::Key::Tab)},
                                      });
    ASSERT_TRUE(shiftTabRelease.has_value())
        << (shiftTabRelease ? "" : shiftTabRelease.error().message);
    auto shiftTabReleaseOutput = producer->produce(tree.context.get(), *shiftTabRelease);
    ASSERT_TRUE(shiftTabReleaseOutput.has_value())
        << (shiftTabReleaseOutput ? "" : shiftTabReleaseOutput.error().message);
    EXPECT_TRUE(shiftTabReleaseOutput->consumption.isConsumed(0));

    expectOk(tree.updater.setDropdownOpen(tree.dropdown, true));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    Platform::GamepadSnapshot dpadHeld{
        .gamepad = gamepad,
        .revision = 82,
    };
    dpadHeld.heldButtons.set(static_cast<usize>(Platform::GamepadButton::DpadDown));
    auto dpadDown = buildFrame(
        *builder, window,
        {
            .frameId = {82},
            .transitions =
                {Platform::GamepadButtonTransition{
                    .routedWindow = window,
                    .gamepad = gamepad,
                    .button = Platform::GamepadButton::DpadDown,
                    .state = Platform::DigitalTransition::Down,
                }},
            .gamepadSnapshots = {dpadHeld},
        });
    ASSERT_TRUE(dpadDown.has_value()) << (dpadDown ? "" : dpadDown.error().message);
    auto dpadDownOutput = producer->produce(tree.context.get(), *dpadDown);
    ASSERT_TRUE(dpadDownOutput.has_value()) << (dpadDownOutput ? "" : dpadDownOutput.error().message);
    EXPECT_TRUE(dpadDownOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.firstItem);

    auto dpadRelease = buildFrame(
        *builder, window,
        {
            .frameId = {83},
            .transitions =
                {Platform::GamepadButtonTransition{
                    .routedWindow = window,
                    .gamepad = gamepad,
                    .button = Platform::GamepadButton::DpadDown,
                    .state = Platform::DigitalTransition::Up,
                }},
            .gamepadSnapshots = {Platform::GamepadSnapshot{.gamepad = gamepad, .revision = 83}},
        });
    ASSERT_TRUE(dpadRelease.has_value()) << (dpadRelease ? "" : dpadRelease.error().message);
    auto dpadReleaseOutput = producer->produce(tree.context.get(), *dpadRelease);
    ASSERT_TRUE(dpadReleaseOutput.has_value())
        << (dpadReleaseOutput ? "" : dpadReleaseOutput.error().message);
    EXPECT_TRUE(dpadReleaseOutput->consumption.isConsumed(0));

    Platform::GamepadSnapshot eastHeld{
        .gamepad = gamepad,
        .revision = 84,
    };
    eastHeld.heldButtons.set(static_cast<usize>(Platform::GamepadButton::East));
    auto cancel = buildFrame(
        *builder, window,
        {
            .frameId = {84},
            .transitions =
                {Platform::GamepadButtonTransition{
                    .routedWindow = window,
                    .gamepad = gamepad,
                    .button = Platform::GamepadButton::East,
                    .state = Platform::DigitalTransition::Down,
                }},
            .gamepadSnapshots = {eastHeld},
        });
    ASSERT_TRUE(cancel.has_value()) << (cancel ? "" : cancel.error().message);
    auto cancelOutput = producer->produce(tree.context.get(), *cancel);
    ASSERT_TRUE(cancelOutput.has_value()) << (cancelOutput ? "" : cancelOutput.error().message);
    EXPECT_TRUE(cancelOutput->consumption.isConsumed(0));
    EXPECT_FALSE(tree.context->activePopup().hasValue());

    auto cancelRelease = buildFrame(
        *builder, window,
        {
            .frameId = {85},
            .transitions =
                {Platform::GamepadButtonTransition{
                    .routedWindow = window,
                    .gamepad = gamepad,
                    .button = Platform::GamepadButton::East,
                    .state = Platform::DigitalTransition::Up,
                }},
            .gamepadSnapshots = {Platform::GamepadSnapshot{.gamepad = gamepad, .revision = 85}},
        });
    ASSERT_TRUE(cancelRelease.has_value()) << (cancelRelease ? "" : cancelRelease.error().message);
    auto cancelReleaseOutput = producer->produce(tree.context.get(), *cancelRelease);
    ASSERT_TRUE(cancelReleaseOutput.has_value())
        << (cancelReleaseOutput ? "" : cancelReleaseOutput.error().message);
    EXPECT_TRUE(cancelReleaseOutput->consumption.isConsumed(0));

    auto closedArrow = buildFrame(*builder, window,
                                  {
                                      .frameId = {86},
                                      .transitions = {keyDown(window, Platform::Key::Down)},
                                      .heldKeys = {Platform::Key::Down},
                                  });
    ASSERT_TRUE(closedArrow.has_value()) << (closedArrow ? "" : closedArrow.error().message);
    auto closedArrowOutput = producer->produce(tree.context.get(), *closedArrow);
    ASSERT_TRUE(closedArrowOutput.has_value())
        << (closedArrowOutput ? "" : closedArrowOutput.error().message);
    EXPECT_FALSE(closedArrowOutput->consumption.isConsumed(0));
}

TEST_F(UIInputRouteProducerTest, FocusedButtonConsumesKeyboardAndGamepadAcceptWithActivationSources)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    std::array<UI::UIButtonActionEvent, 4> activations{};
    usize activationCount = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activations, &activationCount](const UI::UIButtonActionEvent& event) noexcept {
                if (activationCount < activations.size())
                {
                    activations[activationCount] = event;
                }
                ++activationCount;
            }}));

    auto tabFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {70},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(tabFrame.has_value()) << (tabFrame ? "" : tabFrame.error().message);
    auto tabOutput = producer->produce(tree.context.get(), *tabFrame);
    ASSERT_TRUE(tabOutput.has_value()) << (tabOutput ? "" : tabOutput.error().message);
    EXPECT_TRUE(tabOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.target);
    EXPECT_EQ(activationCount, 0U);

    auto keyboardFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {71},
            .transitions = {
                keyDown(window, Platform::Key::Enter),
                keyDown(window, Platform::Key::Space),
                keyDown(window, Platform::Key::KeypadEnter),
            },
            .heldKeys = {
                Platform::Key::Enter,
                Platform::Key::Space,
                Platform::Key::KeypadEnter,
            },
        });
    ASSERT_TRUE(keyboardFrame.has_value())
        << (keyboardFrame ? "" : keyboardFrame.error().message);
    auto keyboardOutput = producer->produce(tree.context.get(), *keyboardFrame);
    ASSERT_TRUE(keyboardOutput.has_value())
        << (keyboardOutput ? "" : keyboardOutput.error().message);
    ASSERT_EQ(keyboardFrame->inputTransitions().size(), 3U);
    EXPECT_TRUE(keyboardOutput->consumption.isConsumed(0));
    EXPECT_TRUE(keyboardOutput->consumption.isConsumed(1));
    EXPECT_TRUE(keyboardOutput->consumption.isConsumed(2));
    ASSERT_EQ(activationCount, 3U);
    for (usize index = 0; index < 3; ++index)
    {
        EXPECT_EQ(activations[index].buttonNode, tree.target);
        EXPECT_EQ(activations[index].source, UI::UIButtonActivationSource::Keyboard);
        EXPECT_EQ(activations[index].platformFrame, keyboardFrame->id());
        EXPECT_EQ(
            activations[index].sourceSequence,
            keyboardFrame->inputTransitions()[index].sequence);
    }

    auto gamepadFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {72},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 72)},
        });
    ASSERT_TRUE(gamepadFrame.has_value()) << (gamepadFrame ? "" : gamepadFrame.error().message);
    auto gamepadOutput = producer->produce(tree.context.get(), *gamepadFrame);
    ASSERT_TRUE(gamepadOutput.has_value()) << (gamepadOutput ? "" : gamepadOutput.error().message);
    EXPECT_TRUE(gamepadOutput->consumption.isConsumed(0));
    ASSERT_EQ(activationCount, 4U);
    EXPECT_EQ(activations[3].buttonNode, tree.target);
    EXPECT_EQ(activations[3].source, UI::UIButtonActivationSource::Gamepad);
    EXPECT_EQ(activations[3].platformFrame, gamepadFrame->id());
    EXPECT_EQ(
        activations[3].sourceSequence,
        gamepadFrame->inputTransitions()[0].sequence);
}

TEST_F(UIInputRouteProducerTest, KeyboardAcceptDownUpTracksEachControlAndPaintState)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    std::array<UI::UIButtonActionEvent, 4> activations{};
    usize activationCount = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activations, &activationCount](const UI::UIButtonActionEvent& event) noexcept {
                if (activationCount < activations.size()) {
                    activations[activationCount] = event;
                }
                ++activationCount;
            }}));

    auto tab = buildFrame(
        *builder,
        window,
        {
            .frameId = {100},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(tab.has_value()) << (tab ? "" : tab.error().message);
    ASSERT_TRUE(producer->produce(tree.context.get(), *tab).has_value());

    auto down = buildFrame(
        *builder,
        window,
        {
            .frameId = {101},
            .transitions = {
                keyDown(window, Platform::Key::Enter),
                keyDown(window, Platform::Key::Space),
            },
            .heldKeys = {Platform::Key::Enter, Platform::Key::Space},
        });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));
    EXPECT_TRUE(downOutput->consumption.isConsumed(1));
    EXPECT_EQ(activationCount, 2U);
    auto pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    EXPECT_TRUE(*pressed);

    auto enterUp = buildFrame(
        *builder,
        window,
        {
            .frameId = {102},
            .transitions = {keyUp(window, Platform::Key::Enter)},
            .heldKeys = {Platform::Key::Space},
        });
    ASSERT_TRUE(enterUp.has_value()) << (enterUp ? "" : enterUp.error().message);
    auto enterUpOutput = producer->produce(tree.context.get(), *enterUp);
    ASSERT_TRUE(enterUpOutput.has_value())
        << (enterUpOutput ? "" : enterUpOutput.error().message);
    EXPECT_TRUE(enterUpOutput->consumption.isConsumed(0));
    pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_TRUE(*pressed);

    auto spaceUp = buildFrame(
        *builder,
        window,
        {
            .frameId = {103},
            .transitions = {keyUp(window, Platform::Key::Space)},
        });
    ASSERT_TRUE(spaceUp.has_value()) << (spaceUp ? "" : spaceUp.error().message);
    auto spaceUpOutput = producer->produce(tree.context.get(), *spaceUp);
    ASSERT_TRUE(spaceUpOutput.has_value())
        << (spaceUpOutput ? "" : spaceUpOutput.error().message);
    EXPECT_TRUE(spaceUpOutput->consumption.isConsumed(0));
    pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_FALSE(*pressed);

    auto gamepadDown = buildFrame(
        *builder,
        window,
        {
            .frameId = {104},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 104)},
        });
    ASSERT_TRUE(gamepadDown.has_value())
        << (gamepadDown ? "" : gamepadDown.error().message);
    auto gamepadDownOutput = producer->produce(tree.context.get(), *gamepadDown);
    ASSERT_TRUE(gamepadDownOutput.has_value())
        << (gamepadDownOutput ? "" : gamepadDownOutput.error().message);
    EXPECT_TRUE(gamepadDownOutput->consumption.isConsumed(0));
    EXPECT_EQ(activationCount, 3U);
    EXPECT_EQ(activations[2].source, UI::UIButtonActivationSource::Gamepad);
    pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_TRUE(*pressed);

    auto gamepadUp = buildFrame(
        *builder,
        window,
        {
            .frameId = {105},
            .transitions = {gamepadButtonUp(window, gamepad)},
            .gamepadSnapshots = {releasedSouthSnapshot(gamepad, 105)},
        });
    ASSERT_TRUE(gamepadUp.has_value())
        << (gamepadUp ? "" : gamepadUp.error().message);
    auto gamepadUpOutput = producer->produce(tree.context.get(), *gamepadUp);
    ASSERT_TRUE(gamepadUpOutput.has_value())
        << (gamepadUpOutput ? "" : gamepadUpOutput.error().message);
    EXPECT_TRUE(gamepadUpOutput->consumption.isConsumed(0));
    pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_FALSE(*pressed);
}

TEST_F(UIInputRouteProducerTest, DisabledButtonDoesNotConsumeAcceptOrInvokeAction)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    usize activationCount = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activationCount](const UI::UIButtonActionEvent&) noexcept {
                ++activationCount;
            }}));
    expectOk(tree.updater.setEnabled(tree.target, false));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto frame = buildFrame(
        *builder,
        window,
        {
            .frameId = {80},
            .transitions = {
                keyDown(window, Platform::Key::Tab),
                keyDown(window, Platform::Key::Enter),
                keyDown(window, Platform::Key::Space),
                keyDown(window, Platform::Key::KeypadEnter),
                gamepadButtonDown(window, gamepad),
            },
            .heldKeys = {
                Platform::Key::Tab,
                Platform::Key::Enter,
                Platform::Key::Space,
                Platform::Key::KeypadEnter,
            },
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 80)},
        });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    ASSERT_EQ(frame->inputTransitions().size(), 5U);
    for (usize ordinal = 0; ordinal < frame->inputTransitions().size(); ++ordinal)
    {
        EXPECT_FALSE(output->consumption.isConsumed(ordinal));
    }
    EXPECT_FALSE(tree.context->defaultActionFocus().hasValue());
    EXPECT_EQ(activationCount, 0U);
}

TEST_F(UIInputRouteProducerTest, PublishesDeduplicatedHeldPointerClaimsAndDropsNonHeldRequests)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Move, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
        }});
    ASSERT_TRUE(token);

    auto heldFrame = buildFrame(*builder, window,
                                {
                                    .frameId = {2},
                                    .transitions = {pointerMove(window, 10.0, 10.0)},
                                    .heldPointerButtons = {Platform::PointerButton::Primary},
                                    .pointerX = 10.0,
                                    .pointerY = 10.0,
                                });
    ASSERT_TRUE(heldFrame.has_value()) << (heldFrame ? "" : heldFrame.error().message);
    auto heldOutput = producer->produce(tree.context.get(), *heldFrame);
    ASSERT_TRUE(heldOutput.has_value()) << (heldOutput ? "" : heldOutput.error().message);
    ASSERT_EQ(heldOutput->claims.controls.size(), 1U);
    const auto* heldClaim = std::get_if<Platform::PointerButtonControlIdentity>(
        &heldOutput->claims.controls.front().control);
    ASSERT_NE(heldClaim, nullptr);
    EXPECT_EQ(heldClaim->window, window);
    EXPECT_EQ(heldClaim->pointer, Platform::PrimaryPointerId);
    EXPECT_EQ(heldClaim->button, Platform::PointerButton::Primary);
    EXPECT_FALSE(heldOutput->consumption.isConsumed(0));

    auto releasedFrame = buildFrame(*builder, window,
                                    {
                                        .frameId = {3},
                                        .transitions = {pointerMove(window, 10.0, 10.0)},
                                        .pointerX = 10.0,
                                        .pointerY = 10.0,
                                    });
    ASSERT_TRUE(releasedFrame.has_value()) << (releasedFrame ? "" : releasedFrame.error().message);
    auto releasedOutput = producer->produce(tree.context.get(), *releasedFrame);
    ASSERT_TRUE(releasedOutput.has_value()) << (releasedOutput ? "" : releasedOutput.error().message);
    EXPECT_TRUE(releasedOutput->claims.controls.empty());
}

TEST_F(UIInputRouteProducerTest, ClaimCapacityFailurePreservesPublishedClaimsAndConsumesAttemptWatermark)
{
    auto producer = createProducer(128, 1);
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Move, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Secondary));
        }});
    ASSERT_TRUE(token);

    auto firstFrame = buildFrame(*builder, window,
                                 {
                                     .frameId = {4},
                                     .transitions = {pointerMove(window, 10.0, 10.0)},
                                     .heldPointerButtons = {Platform::PointerButton::Primary},
                                     .pointerX = 10.0,
                                     .pointerY = 10.0,
                                 });
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    auto firstOutput = producer->produce(tree.context.get(), *firstFrame);
    ASSERT_TRUE(firstOutput.has_value()) << (firstOutput ? "" : firstOutput.error().message);
    ASSERT_EQ(firstOutput->claims.controls.size(), 1U);
    EXPECT_EQ(firstOutput->claims.platformFrame, Platform::PlatformFrameId{4});

    auto overflowingFrame = buildFrame(*builder, window,
                                       {
                                           .frameId = {5},
                                           .transitions = {pointerMove(window, 10.0, 10.0)},
                                           .heldPointerButtons = {Platform::PointerButton::Primary,
                                                                  Platform::PointerButton::Secondary},
                                           .pointerX = 10.0,
                                           .pointerY = 10.0,
                                       });
    ASSERT_TRUE(overflowingFrame.has_value()) << (overflowingFrame ? "" : overflowingFrame.error().message);
    auto failed = producer->produce(tree.context.get(), *overflowingFrame);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_EQ(firstOutput->claims.platformFrame, Platform::PlatformFrameId{4});
    ASSERT_EQ(firstOutput->claims.controls.size(), 1U);

    auto retry = producer->produce(tree.context.get(), *overflowingFrame);
    ASSERT_FALSE(retry.has_value());
    EXPECT_EQ(retry.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(UIInputRouteProducerTest, MapsMixedRawOrdinalsWithHolesToFrameAndSequence)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    PointerEventTrace observed;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&observed](UI::UIRoutedPointerEvent& event) noexcept {
            observed.push(event.input());
            event.consumeInputTransition();
        }});
    ASSERT_TRUE(token);

    FrameSpec spec{
        .frameId = {7},
        .transitions =
            {
                keyDown(window, Platform::Key::A),
                pointerMove(window, 90.0, 90.0),
                keyDown(window, Platform::Key::B),
                pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
            },
        .heldKeys = {Platform::Key::A, Platform::Key::B},
        .heldPointerButtons = {Platform::PointerButton::Primary},
        .pointerX = 10.0,
        .pointerY = 10.0,
    };
    auto frame = buildFrame(*builder, window, spec);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    ASSERT_EQ(observed.size, 1U);
    EXPECT_EQ(observed.events[0].platformFrame, frame->id());
    EXPECT_EQ(observed.events[0].transitionOrdinal, 3U);
    EXPECT_EQ(observed.events[0].sourceSequence, frame->inputTransitions()[3].sequence);
    EXPECT_TRUE(output->consumption.isConsumed(3));
    EXPECT_FALSE(output->consumption.isConsumed(0));
    EXPECT_FALSE(output->consumption.isConsumed(1));
    EXPECT_FALSE(output->consumption.isConsumed(2));
}

TEST_F(UIInputRouteProducerTest, PreservesPointerTransitionPositions)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    PointerEventTrace observed;
    auto buttonToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&observed](UI::UIRoutedPointerEvent& event) noexcept {
            observed.push(event.input());
            event.consumeInputTransition();
        }});
    auto wheelToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Wheel, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&observed](UI::UIRoutedPointerEvent& event) noexcept {
            observed.push(event.input());
            event.consumeInputTransition();
        }});
    ASSERT_TRUE(buttonToken && wheelToken);

    FrameSpec spec{
        .frameId = {8},
        .transitions =
            {
                pointerMove(window, 10.0, 10.0, 1.0, 1.0),
                pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
                pointerMove(window, 30.0, 30.0, 20.0, 20.0),
                pointerWheel(window, 30.0, 30.0, 0.0, -1.0),
                pointerMove(window, 50.0, 50.0, 20.0, 20.0),
            },
        .heldPointerButtons = {Platform::PointerButton::Primary},
        .pointerX = 50.0,
        .pointerY = 50.0,
        .accumulatedDeltaX = 41.0,
        .accumulatedDeltaY = 41.0,
    };
    auto frame = buildFrame(*builder, window, spec);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    ASSERT_EQ(observed.size, 2U);
    EXPECT_EQ(observed.events[0].kind, UI::UIRoutedPointerEventKind::ButtonDown);
    EXPECT_FLOAT_EQ(observed.events[0].position.x, 10.0F);
    EXPECT_FLOAT_EQ(observed.events[0].position.y, 10.0F);
    EXPECT_EQ(observed.events[1].kind, UI::UIRoutedPointerEventKind::Wheel);
    EXPECT_FLOAT_EQ(observed.events[1].position.x, 30.0F);
    EXPECT_FLOAT_EQ(observed.events[1].position.y, 30.0F);
    EXPECT_TRUE(output->consumption.isConsumed(1));
    EXPECT_TRUE(output->consumption.isConsumed(3));
}

TEST_F(UIInputRouteProducerTest, ConsumedBitsCoverOrdinalsSixtyThreeAndSixtyFourAndClearNextFrame)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto consumeButton = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.consumeInputTransition(); }});
    auto consumeWheel = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Wheel, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.consumeInputTransition(); }});
    ASSERT_TRUE(consumeButton && consumeWheel);

    FrameSpec first{.frameId = {63}};
    for (usize index = 0; index < 63; ++index)
    {
        first.transitions.push_back(keyDown(window, Platform::Key::A));
    }
    first.transitions.push_back(pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0));
    first.transitions.push_back(pointerWheel(window, 10.0, 10.0, 0.0, 1.0));
    first.heldKeys = {Platform::Key::A};
    first.heldPointerButtons = {Platform::PointerButton::Primary};
    first.pointerX = 10.0;
    first.pointerY = 10.0;
    auto firstFrame = buildFrame(*builder, window, first);
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);

    auto firstOutput = producer->produce(tree.context.get(), *firstFrame);
    ASSERT_TRUE(firstOutput.has_value()) << (firstOutput ? "" : firstOutput.error().message);
    EXPECT_TRUE(firstOutput->consumption.isConsumed(63));
    EXPECT_TRUE(firstOutput->consumption.isConsumed(64));
    ASSERT_EQ(firstOutput->consumption.consumedOrdinalWords.size(), 2U);

    FrameSpec second{
        .frameId = {64},
        .transitions = {pointerMove(window, 10.0, 10.0)},
        .pointerX = 10.0,
        .pointerY = 10.0,
    };
    auto secondFrame = buildFrame(*builder, window, second);
    ASSERT_TRUE(secondFrame.has_value()) << (secondFrame ? "" : secondFrame.error().message);
    auto secondOutput = producer->produce(nullptr, *secondFrame);
    ASSERT_TRUE(secondOutput.has_value()) << (secondOutput ? "" : secondOutput.error().message);
    EXPECT_FALSE(secondOutput->consumption.isConsumed(0));
    EXPECT_TRUE(secondOutput->consumption.consumedOrdinalWords.empty());
}

TEST_F(UIInputRouteProducerTest, NoHitDoesNotConsumeAndStoppedButtonDefaultStillConsumes)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent& event) noexcept {
            ++callbackCount;
            event.stopPropagation();
        }});
    ASSERT_TRUE(token);

    auto noHitFrame =
        buildFrame(*builder, window,
                   {
                       .frameId = {9},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 90.0, 90.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 90.0,
                       .pointerY = 90.0,
                   });
    ASSERT_TRUE(noHitFrame.has_value()) << (noHitFrame ? "" : noHitFrame.error().message);
    auto noHitOutput = producer->produce(tree.context.get(), *noHitFrame);
    ASSERT_TRUE(noHitOutput.has_value()) << (noHitOutput ? "" : noHitOutput.error().message);
    EXPECT_EQ(callbackCount, 0U);
    EXPECT_FALSE(noHitOutput->consumption.isConsumed(0));

    auto stoppedFrame =
        buildFrame(*builder, window,
                   {
                       .frameId = {10},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(stoppedFrame.has_value()) << (stoppedFrame ? "" : stoppedFrame.error().message);
    auto stoppedOutput = producer->produce(tree.context.get(), *stoppedFrame);
    ASSERT_TRUE(stoppedOutput.has_value()) << (stoppedOutput ? "" : stoppedOutput.error().message);
    EXPECT_EQ(callbackCount, 1U);
    EXPECT_TRUE(stoppedOutput->consumption.isConsumed(0));
}

TEST_F(UIInputRouteProducerTest, OwnerMismatchFailsBeforeAnyCallback)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(otherWindow);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept { ++callbackCount; }});
    ASSERT_TRUE(token);

    auto frame = buildFrame(*builder, window,
                            {
                                .frameId = {11},
                                .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                                .heldPointerButtons = {Platform::PointerButton::Primary},
                                .pointerX = 10.0,
                                .pointerY = 10.0,
                            });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(callbackCount, 0U);
}

TEST_F(UIInputRouteProducerTest, ResetCancelAndNonPointerTransitionsDoNotRouteOrFabricateUp)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto upToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonUp, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept { ++callbackCount; }});
    ASSERT_TRUE(upToken);

    FrameSpec spec{
        .frameId = {12},
        .transitions =
            {
                keyDown(window, Platform::Key::A),
                Platform::InputCancelTransition{
                    .routedWindow = window,
                    .reason = Platform::InputCancelReason::FocusLost,
                },
                Platform::InputStreamReset{
                    .routedWindow = window,
                    .reason = Platform::InputResetReason::BackendRecovery,
                },
            },
        .heldKeys = {Platform::Key::A},
        .pointerX = 10.0,
        .pointerY = 10.0,
    };
    auto frame = buildFrame(*builder, window, spec);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_EQ(callbackCount, 0U);
    for (usize ordinal = 0; ordinal < frame->inputTransitions().size(); ++ordinal)
    {
        EXPECT_FALSE(output->consumption.isConsumed(ordinal));
    }
}

TEST_F(UIInputRouteProducerTest, RouteFailureDoesNotPublishOrReplayEarlierListenerSideEffects)
{
    auto producer = createProducer();
    RouteTree goodTree = createRouteTree(window);
    RouteTree failingTree = createRouteTree(window, {
                                                        .nodeCapacity = 8,
                                                        .rootCapacity = 1,
                                                        .routePathCapacity = 1,
                                                        .routedPointerListenerCapacity = 16,
                                                    });
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(goodTree.context, nullptr);
    ASSERT_NE(failingTree.context, nullptr);
    expectOk(
        failingTree.updater.setPointerHitPolicy(failingTree.root.rootNodeId(), UI::UIPointerHitPolicy::Targetable));
    expectOk(failingTree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto consumeToken = addListener(
        *goodTree.context,
        {.node = goodTree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.consumeInputTransition(); }});
    usize failingCallbackCount = 0;
    auto failingToken =
        addListener(*failingTree.context,
                    {.node = failingTree.root.rootNodeId(),
                     .kind = UI::UIRoutedPointerEventKind::Move,
                     .phases = UI::UIEventPhaseMask::Target},
                    UI::UIRoutedPointerCallback{
                        [&failingCallbackCount](UI::UIRoutedPointerEvent&) noexcept { ++failingCallbackCount; }});
    ASSERT_TRUE(consumeToken && failingToken);

    auto firstFrame =
        buildFrame(*builder, window,
                   {
                       .frameId = {13},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    auto firstOutput = producer->produce(goodTree.context.get(), *firstFrame);
    ASSERT_TRUE(firstOutput.has_value()) << (firstOutput ? "" : firstOutput.error().message);
    EXPECT_TRUE(firstOutput->consumption.isConsumed(0));

    auto failingFrame = buildFrame(*builder, window,
                                   {
                                       .frameId = {14},
                                       .transitions =
                                           {
                                               pointerMove(window, 90.0, 90.0),
                                               pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
                                           },
                                       .heldPointerButtons = {Platform::PointerButton::Primary},
                                       .pointerX = 10.0,
                                       .pointerY = 10.0,
                                   });
    ASSERT_TRUE(failingFrame.has_value()) << (failingFrame ? "" : failingFrame.error().message);
    auto failedOutput = producer->produce(failingTree.context.get(), *failingFrame);
    EXPECT_FALSE(failedOutput.has_value());
    EXPECT_EQ(failingCallbackCount, 1U);
    EXPECT_EQ(firstOutput->consumption.platformFrame, Platform::PlatformFrameId{13});
    EXPECT_TRUE(firstOutput->consumption.isConsumed(0));

    auto sameFrameRetry = producer->produce(failingTree.context.get(), *failingFrame);
    EXPECT_FALSE(sameFrameRetry.has_value());
    EXPECT_EQ(failingCallbackCount, 1U);

    auto cleanFrame = buildFrame(*builder, window,
                                 {
                                     .frameId = {15},
                                     .transitions = {pointerMove(window, 90.0, 90.0)},
                                     .pointerX = 90.0,
                                     .pointerY = 90.0,
                                 });
    ASSERT_TRUE(cleanFrame.has_value()) << (cleanFrame ? "" : cleanFrame.error().message);
    auto cleanOutput = producer->produce(nullptr, *cleanFrame);
    ASSERT_TRUE(cleanOutput.has_value()) << (cleanOutput ? "" : cleanOutput.error().message);
    EXPECT_FALSE(cleanOutput->consumption.isConsumed(0));
}

TEST_F(UIInputRouteProducerTest, ThreeHundredFramesPerformNoObservedPmrAllocations)
{
    ObservingMemoryResource resource;
    auto producer = createProducer(
        128, InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity, resource);
    RouteTree tree = createRouteTree(window,
                                     {
                                         .nodeCapacity = 8,
                                         .rootCapacity = 1,
                                         .routePathCapacity = 8,
                                         .routedPointerListenerCapacity = 16,
                                     },
                                     resource);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Move, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            (void)event.claimPointerButton(Platform::PointerButton::Primary);
        }});
    ASSERT_TRUE(token);
    const usize allocationCountBeforeFrames = resource.allocationCount();
    ASSERT_GT(allocationCountBeforeFrames, 0U);

    for (u64 frameIndex = 1; frameIndex <= 300; ++frameIndex)
    {
        auto frame = buildFrame(*builder, window,
                                {
                                    .frameId = {frameIndex},
                                    .transitions = {pointerMove(window, 10.0, 10.0)},
                                    .heldPointerButtons = {Platform::PointerButton::Primary},
                                    .pointerX = 10.0,
                                    .pointerY = 10.0,
                                });
        ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
        auto output = producer->produce(tree.context.get(), *frame);
        ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
        EXPECT_EQ(output->consumption.platformFrame, frame->id());
        EXPECT_EQ(output->claims.controls.size(), 1U);
    }
    EXPECT_EQ(resource.allocationCount(), allocationCountBeforeFrames);
}

TEST_F(UIInputRouteProducerTest, CapacityValidationAllowsOneReservedResetSlot)
{
    EXPECT_FALSE(UIInputRouteProducer::Create(
                     0, InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity)
                     .has_value());
    EXPECT_FALSE(UIInputRouteProducer::Create(
                     static_cast<usize>(Platform::PlatformFrameCapacityConfig::MaximumInputTransitionCapacity) + 1U,
                     InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity)
                     .has_value());
    EXPECT_FALSE(UIInputRouteProducer::Create(1, 0).has_value());
    EXPECT_FALSE(UIInputRouteProducer::Create(
                     1, InputActionMapperCapacityConfig::MaximumContinuousControlClaimCapacity + 1U)
                     .has_value());

    auto smallBuilder = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 1,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(smallBuilder.has_value()) << (smallBuilder ? "" : smallBuilder.error().message);
    auto producer = createProducer(1);
    ASSERT_NE(producer, nullptr);

    auto frame = buildFrame(*smallBuilder, window,
                            {
                                .frameId = {16},
                                .transitions =
                                    {
                                        keyDown(window, Platform::Key::A),
                                        Platform::InputStreamReset{
                                            .routedWindow = window,
                                            .reason = Platform::InputResetReason::BackendRecovery,
                                        },
                                    },
                                .heldKeys = {Platform::Key::A},
                            });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    ASSERT_EQ(frame->inputTransitions().size(), 2U);
    EXPECT_NE(std::get_if<Platform::InputStreamReset>(&frame->inputTransitions()[1].payload), nullptr);

    auto output = producer->produce(nullptr, *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_EQ(output->consumption.transitionCount, 2U);
    EXPECT_TRUE(output->consumption.consumedOrdinalWords.empty());
    EXPECT_TRUE(output->claims.controls.empty());
}

TEST_F(UIInputRouteProducerTest, FloatUnrepresentablePointerValueFailsBeforeAnyCallback)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept { ++callbackCount; }});
    ASSERT_TRUE(token);

    const double unrepresentable = static_cast<double>((std::numeric_limits<float>::max)()) * 2.0;
    auto frame = buildFrame(*builder, window,
                            {
                                .frameId = {17},
                                .transitions =
                                    {
                                        pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
                                        pointerWheel(window, unrepresentable, 10.0, 0.0, 1.0),
                                    },
                                .heldPointerButtons = {Platform::PointerButton::Primary},
                                .pointerX = 10.0,
                                .pointerY = 10.0,
                            });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(callbackCount, 0U);
}

TEST_F(UIInputRouteProducerTest, ButtonDefaultDownSuppressesGameplayUntilTrueUpThenRestoresUnconsumedDown)
{
    auto producer = createProducer();
    auto mapper = createPointerMapper();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto consumedDown =
        buildFrame(*builder, window,
                   {
                       .frameId = {21},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(consumedDown.has_value()) << (consumedDown ? "" : consumedDown.error().message);
    auto consumedOutput = producer->produce(tree.context.get(), *consumedDown);
    ASSERT_TRUE(consumedOutput.has_value()) << (consumedOutput ? "" : consumedOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*consumedDown, consumedOutput->consumption, consumedOutput->claims, 0, 0,
                               &lastPresentedCamera2D)
                    .has_value());
    auto suppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressed.has_value()) << (suppressed ? "" : suppressed.error().message);
    EXPECT_TRUE(suppressed->transitions.empty());
    EXPECT_FALSE(suppressed->isActive(PointerAction));

    auto stillHeld = buildFrame(*builder, window,
                                {
                                    .frameId = {22},
                                    .heldPointerButtons = {Platform::PointerButton::Primary},
                                    .pointerX = 10.0,
                                    .pointerY = 10.0,
                                });
    ASSERT_TRUE(stillHeld.has_value()) << (stillHeld ? "" : stillHeld.error().message);
    auto stillHeldOutput = producer->produce(nullptr, *stillHeld);
    ASSERT_TRUE(stillHeldOutput.has_value()) << (stillHeldOutput ? "" : stillHeldOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*stillHeld, stillHeldOutput->consumption, stillHeldOutput->claims, 1, 0,
                               &lastPresentedCamera2D)
                    .has_value());
    auto stillSuppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(stillSuppressed.has_value()) << (stillSuppressed ? "" : stillSuppressed.error().message);
    EXPECT_TRUE(stillSuppressed->transitions.empty());

    auto trueUp = buildFrame(*builder, window,
                             {
                                 .frameId = {23},
                                 .transitions = {pointerButton(window, Platform::DigitalTransition::Up, 10.0, 10.0)},
                                 .pointerX = 10.0,
                                 .pointerY = 10.0,
                             });
    ASSERT_TRUE(trueUp.has_value()) << (trueUp ? "" : trueUp.error().message);
    auto upOutput = producer->produce(nullptr, *trueUp);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*trueUp, upOutput->consumption, upOutput->claims, 2, 0, &lastPresentedCamera2D)
                    .has_value());
    auto afterUp = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(afterUp.has_value()) << (afterUp ? "" : afterUp.error().message);
    EXPECT_TRUE(afterUp->transitions.empty());

    auto downAgain =
        buildFrame(*builder, window,
                   {
                       .frameId = {24},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(downAgain.has_value()) << (downAgain ? "" : downAgain.error().message);
    auto downAgainOutput = producer->produce(nullptr, *downAgain);
    ASSERT_TRUE(downAgainOutput.has_value()) << (downAgainOutput ? "" : downAgainOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*downAgain, downAgainOutput->consumption, downAgainOutput->claims, 3, 0,
                               &lastPresentedCamera2D)
                    .has_value());
    auto restored = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(restored.has_value()) << (restored ? "" : restored.error().message);
    ASSERT_EQ(restored->transitions.size(), 1U);
    ASSERT_NE(digital(restored->transitions[0]), nullptr);
    EXPECT_EQ(digital(restored->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(restored->isActive(PointerAction));
}

TEST_F(UIInputRouteProducerTest, HeldPointerClaimCancelsObservedGameplayUntilTrueUp)
{
    auto producer = createProducer();
    auto mapper = createPointerMapper();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);
    auto claimToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Move, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
        }});
    ASSERT_TRUE(claimToken);

    auto down = buildFrame(*builder, window,
                           {
                               .frameId = {31},
                               .transitions = {
                                   pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                               .heldPointerButtons = {Platform::PointerButton::Primary},
                               .pointerX = 10.0,
                               .pointerY = 10.0,
                           });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(nullptr, *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*down, downOutput->consumption, downOutput->claims, 0, 0, &lastPresentedCamera2D)
                    .has_value());
    auto pressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    ASSERT_EQ(pressed->transitions.size(), 1U);
    ASSERT_NE(digital(pressed->transitions[0]), nullptr);
    EXPECT_EQ(digital(pressed->transitions[0])->kind, InputActionTransitionKind::Started);
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    auto claimed = buildFrame(*builder, window,
                              {
                                  .frameId = {32},
                                  .transitions = {pointerMove(window, 10.0, 10.0)},
                                  .heldPointerButtons = {Platform::PointerButton::Primary},
                                  .pointerX = 10.0,
                                  .pointerY = 10.0,
                              });
    ASSERT_TRUE(claimed.has_value()) << (claimed ? "" : claimed.error().message);
    auto claimedOutput = producer->produce(tree.context.get(), *claimed);
    ASSERT_TRUE(claimedOutput.has_value()) << (claimedOutput ? "" : claimedOutput.error().message);
    ASSERT_EQ(claimedOutput->claims.controls.size(), 1U);
    ASSERT_TRUE(mapper
                    ->mapFrame(*claimed, claimedOutput->consumption, claimedOutput->claims, 1, 1,
                               &lastPresentedCamera2D)
                    .has_value());
    auto cancelled = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(cancelled.has_value()) << (cancelled ? "" : cancelled.error().message);
    ASSERT_EQ(cancelled->transitions.size(), 1U);
    ASSERT_NE(digital(cancelled->transitions[0]), nullptr);
    EXPECT_EQ(digital(cancelled->transitions[0])->kind, InputActionTransitionKind::Cancelled);
    EXPECT_FALSE(digital(cancelled->transitions[0])->worldPointerSample.has_value());
    EXPECT_FALSE(cancelled->isActive(PointerAction));
    ASSERT_TRUE(mapper->completeSimulationTick(1).has_value());

    auto up = buildFrame(*builder, window,
                         {
                             .frameId = {33},
                             .transitions = {
                                 pointerButton(window, Platform::DigitalTransition::Up, 10.0, 10.0)},
                             .pointerX = 10.0,
                             .pointerY = 10.0,
                         });
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    auto upOutput = producer->produce(nullptr, *up);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*up, upOutput->consumption, upOutput->claims, 2, 2, &lastPresentedCamera2D)
                    .has_value());
    auto releasedSuppression = mapper->simulationActionsForTick(2);
    ASSERT_TRUE(releasedSuppression.has_value())
        << (releasedSuppression ? "" : releasedSuppression.error().message);
    EXPECT_TRUE(releasedSuppression->transitions.empty());
    EXPECT_FALSE(releasedSuppression->isActive(PointerAction));
}

TEST_F(UIInputRouteProducerTest, PointerClaimInterceptsInitialDownWithoutTransitionConsumption)
{
    auto producer = createProducer();
    auto mapper = createPointerMapper();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);
    auto claimToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            event.preventDefaultAction();
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
        }});
    ASSERT_TRUE(claimToken);

    auto down = buildFrame(*builder, window,
                           {
                               .frameId = {41},
                               .transitions = {
                                   pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                               .heldPointerButtons = {Platform::PointerButton::Primary},
                               .pointerX = 10.0,
                               .pointerY = 10.0,
                           });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto output = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_FALSE(output->consumption.isConsumed(0));
    ASSERT_EQ(output->claims.controls.size(), 1U);

    ASSERT_TRUE(mapper
                    ->mapFrame(*down, output->consumption, output->claims, 0, 0, &lastPresentedCamera2D)
                    .has_value());
    auto suppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressed.has_value()) << (suppressed ? "" : suppressed.error().message);
    EXPECT_TRUE(suppressed->transitions.empty());
    EXPECT_FALSE(suppressed->isActive(PointerAction));
}

// Product-level M10-A39 gate: HUD Button default action (same surface as
// tina_sample_2d) must consume primary-pointer click so a world/gameplay Action
// bound to Primary does not fire; a miss must still map the world Action.
TEST_F(UIInputRouteProducerTest, ProductButtonClickDoesNotPenetrateWorldPointerAction)
{
    auto producer = createProducer();
    auto mapper = createPointerMapper();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    ASSERT_NE(tree.context, nullptr);

    usize activationCount = 0;
    UI::UIButtonActivationSource lastSource = UI::UIButtonActivationSource::PrimaryPointer;
    u64 lastSourceSequence = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activationCount, &lastSource, &lastSourceSequence](
                const UI::UIButtonActionEvent& event) noexcept {
                ++activationCount;
                lastSource = event.source;
                lastSourceSequence = event.sourceSequence;
            }}));

    // --- Hit path: Down + Up inside Button activates UI and never presses world Action ---
    auto hitDown =
        buildFrame(*builder, window,
                   {
                       .frameId = {60},
                       .transitions =
                           {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(hitDown.has_value()) << (hitDown ? "" : hitDown.error().message);
    auto hitDownOutput = producer->produce(tree.context.get(), *hitDown);
    ASSERT_TRUE(hitDownOutput.has_value()) << (hitDownOutput ? "" : hitDownOutput.error().message);
    EXPECT_TRUE(hitDownOutput->consumption.isConsumed(0));
    auto pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    EXPECT_TRUE(*pressed);
    ASSERT_TRUE(mapper
                    ->mapFrame(*hitDown, hitDownOutput->consumption, hitDownOutput->claims, 0, 0,
                               &lastPresentedCamera2D)
                    .has_value());
    auto suppressedDown = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressedDown.has_value()) << (suppressedDown ? "" : suppressedDown.error().message);
    EXPECT_TRUE(suppressedDown->transitions.empty());
    EXPECT_FALSE(suppressedDown->isActive(PointerAction));
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    auto hitUp =
        buildFrame(*builder, window,
                   {
                       .frameId = {61},
                       .transitions =
                           {pointerButton(window, Platform::DigitalTransition::Up, 10.0, 10.0)},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(hitUp.has_value()) << (hitUp ? "" : hitUp.error().message);
    auto hitUpOutput = producer->produce(tree.context.get(), *hitUp);
    ASSERT_TRUE(hitUpOutput.has_value()) << (hitUpOutput ? "" : hitUpOutput.error().message);
    ASSERT_TRUE(mapper
                    ->mapFrame(*hitUp, hitUpOutput->consumption, hitUpOutput->claims, 1, 1,
                               &lastPresentedCamera2D)
                    .has_value());
    auto afterUp = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(afterUp.has_value()) << (afterUp ? "" : afterUp.error().message);
    EXPECT_TRUE(afterUp->transitions.empty());
    EXPECT_FALSE(afterUp->isActive(PointerAction));
    ASSERT_TRUE(mapper->completeSimulationTick(1).has_value());

    EXPECT_EQ(activationCount, 1U);
    EXPECT_EQ(lastSource, UI::UIButtonActivationSource::PrimaryPointer);
    EXPECT_EQ(lastSourceSequence, hitUp->inputTransitions()[0].sequence);
    auto released = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(released.has_value()) << (released ? "" : released.error().message);
    EXPECT_FALSE(*released);

    // --- Miss path: Down outside Button must map world Pointer Action once ---
    auto missDown =
        buildFrame(*builder, window,
                   {
                       .frameId = {62},
                       .transitions =
                           {pointerButton(window, Platform::DigitalTransition::Down, 90.0, 90.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 90.0,
                       .pointerY = 90.0,
                   });
    ASSERT_TRUE(missDown.has_value()) << (missDown ? "" : missDown.error().message);
    auto missDownOutput = producer->produce(tree.context.get(), *missDown);
    ASSERT_TRUE(missDownOutput.has_value()) << (missDownOutput ? "" : missDownOutput.error().message);
    EXPECT_FALSE(missDownOutput->consumption.isConsumed(0));
    ASSERT_TRUE(mapper
                    ->mapFrame(*missDown, missDownOutput->consumption, missDownOutput->claims, 2, 2,
                               &lastPresentedCamera2D)
                    .has_value());
    auto worldPressed = mapper->simulationActionsForTick(2);
    ASSERT_TRUE(worldPressed.has_value()) << (worldPressed ? "" : worldPressed.error().message);
    ASSERT_EQ(worldPressed->transitions.size(), 1U);
    ASSERT_NE(digital(worldPressed->transitions[0]), nullptr);
    EXPECT_EQ(digital(worldPressed->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(worldPressed->isActive(PointerAction));
    EXPECT_EQ(activationCount, 1U);
}

TEST_F(UIInputRouteProducerTest, CancelAndCoveringResetClearButtonStateWithoutActivation)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    usize activationCount = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activationCount](const UI::UIButtonActionEvent&) noexcept {
                ++activationCount;
            }}));

    const auto routeDown = [&](u64 frameValue) {
        auto down = buildFrame(
            *builder,
            window,
            {
                .frameId = {frameValue},
                .transitions = {
                    pointerButton(
                        window,
                        Platform::DigitalTransition::Down,
                        10.0,
                        10.0)},
                .heldPointerButtons = {Platform::PointerButton::Primary},
                .pointerX = 10.0,
                .pointerY = 10.0,
            });
        EXPECT_TRUE(down.has_value())
            << (down ? "" : down.error().message);
        if (!down) {
            return;
        }
        auto output = producer->produce(tree.context.get(), *down);
        EXPECT_TRUE(output.has_value())
            << (output ? "" : output.error().message);
        if (output) {
            EXPECT_TRUE(output->consumption.isConsumed(0));
        }
        auto pressed = tree.updater.isButtonPressed(tree.target);
        EXPECT_TRUE(pressed.has_value())
            << (pressed ? "" : pressed.error().message);
        if (pressed) {
            EXPECT_TRUE(*pressed);
        }
    };

    const auto routeUpWithoutActivation = [&](u64 frameValue) {
        auto up = buildFrame(
            *builder,
            window,
            {
                .frameId = {frameValue},
                .transitions = {
                    pointerButton(
                        window,
                        Platform::DigitalTransition::Up,
                        10.0,
                        10.0)},
                .pointerX = 10.0,
                .pointerY = 10.0,
            });
        EXPECT_TRUE(up.has_value()) << (up ? "" : up.error().message);
        if (!up) {
            return;
        }
        auto output = producer->produce(tree.context.get(), *up);
        EXPECT_TRUE(output.has_value())
            << (output ? "" : output.error().message);
        if (output) {
            EXPECT_FALSE(output->consumption.isConsumed(0));
        }
        EXPECT_EQ(activationCount, 0U);
    };

    routeDown(50);
    auto cancel = buildFrame(
        *builder,
        window,
        {
            .frameId = {51},
            .transitions = {
                Platform::InputCancelTransition{
                    .routedWindow = window,
                    .reason = Platform::InputCancelReason::FocusLost,
                }},
            .pointerX = 10.0,
            .pointerY = 10.0,
        });
    ASSERT_TRUE(cancel.has_value())
        << (cancel ? "" : cancel.error().message);
    auto cancelOutput = producer->produce(tree.context.get(), *cancel);
    ASSERT_TRUE(cancelOutput.has_value())
        << (cancelOutput ? "" : cancelOutput.error().message);
    EXPECT_FALSE(cancelOutput->consumption.isConsumed(0));
    auto afterCancel = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(afterCancel.has_value())
        << (afterCancel ? "" : afterCancel.error().message);
    EXPECT_FALSE(*afterCancel);
    EXPECT_EQ(activationCount, 0U);
    routeUpWithoutActivation(52);

    routeDown(53);
    auto reset = buildFrame(
        *builder,
        window,
        {
            .frameId = {54},
            .transitions = {
                Platform::InputStreamReset{
                    .routedWindow = std::nullopt,
                    .reason = Platform::InputResetReason::BackendRecovery,
                }},
            .pointerX = 10.0,
            .pointerY = 10.0,
        });
    ASSERT_TRUE(reset.has_value()) << (reset ? "" : reset.error().message);
    auto resetOutput = producer->produce(tree.context.get(), *reset);
    ASSERT_TRUE(resetOutput.has_value())
        << (resetOutput ? "" : resetOutput.error().message);
    EXPECT_FALSE(resetOutput->consumption.isConsumed(0));
    auto afterReset = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(afterReset.has_value())
        << (afterReset ? "" : afterReset.error().message);
    EXPECT_FALSE(*afterReset);
    EXPECT_EQ(activationCount, 0U);
    routeUpWithoutActivation(55);
}

} // namespace
} // namespace Tina::Tests
