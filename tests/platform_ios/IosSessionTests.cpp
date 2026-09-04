#include <tina/platform/ios/IosSession.hpp>

#include "WindowSurfaceLeaseAccess.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <utility>
#include <variant>

namespace Tina::Platform {
namespace {

constexpr std::uintptr_t InitialLayer = 0x1000;
constexpr FramebufferExtent PortraitExtent{1170, 2532};
constexpr ContentScale Retina3x{3.0F, 3.0F};
constexpr i32 HidKeyboardA = 0x04;
constexpr i32 HidKeyboardVolumeUp = 0x80;

[[nodiscard]] std::unique_ptr<IosSession> makeSession()
{
    auto created = IosSession::Create();
    EXPECT_TRUE(created.has_value());
    return created.has_value() ? std::move(*created) : nullptr;
}

[[nodiscard]] const WindowFrameSnapshot* pollWindow(IosSession& session)
{
    auto poll = session.pollFrame();
    EXPECT_TRUE(poll.has_value());
    if (!poll.has_value() || !poll->isContinueFrame() || poll->frame() == nullptr)
    {
        return nullptr;
    }
    return poll->frame()->primaryWindow();
}

TEST(IosSessionTest, CreateWithoutALayerHasNoBackendAndPollsExit)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->facet(), nullptr);
    EXPECT_EQ(session->backend(), nullptr);

    auto poll = session->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->isExitRequested());
}

TEST(IosSessionTest, RejectsAMissingLayer)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);

    auto status = session->bindLayer(IosNativeLayerHandle{.metalLayer = 0}, PortraitExtent, Retina3x);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_EQ(session->facet(), nullptr);
}

TEST(IosSessionTest, FirstBindCreatesTheBackendAndAnIosLease)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);

    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());
    ASSERT_NE(session->facet(), nullptr);
    ASSERT_NE(session->backend(), nullptr);

    auto lease = session->backend()->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(lease.has_value());
    auto binding = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(*lease);
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(binding->kind, Integration::Detail::NativeWindowBindingKind::Ios);
    EXPECT_EQ(binding->nativeDisplay, 0U);
    EXPECT_EQ(binding->nativeWindow, InitialLayer);
}

TEST(IosSessionTest, TouchIdentityZeroIsRejected)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    EXPECT_FALSE(session->onTouch(0, IosTouchPhase::Began, 10.0F, 20.0F));

    const WindowFrameSnapshot* window = pollWindow(*session);
    ASSERT_NE(window, nullptr);
    EXPECT_FALSE(window->input.pointers[0].present);
}

TEST(IosSessionTest, BeganMapsToSlotZeroAndPointsStayLogical)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    EXPECT_TRUE(session->onTouch(0x50, IosTouchPhase::Began, 100.0F, 200.0F));

    auto poll = session->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->input.pointers[0].present);
    EXPECT_DOUBLE_EQ(window->input.pointers[0].logicalX, 100.0)
        << "locationInView is already points; dividing by 3x is the bug";
    EXPECT_DOUBLE_EQ(window->input.pointers[0].logicalY, 200.0);

    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* button = std::get_if<PointerButtonTransition>(&transitions[0].payload);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->pointer, 0);
    EXPECT_EQ(button->state, DigitalTransition::Down);
}

TEST(IosSessionTest, ADuplicateBeganKeepsTheSlot)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    EXPECT_TRUE(session->onTouch(0x50, IosTouchPhase::Began, 10.0F, 10.0F));
    EXPECT_TRUE(session->onTouch(0x50, IosTouchPhase::Began, 12.0F, 12.0F))
        << "a duplicated Began must keep the mapping rather than drop the finger";

    auto poll = session->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.pointers[0].present);
    EXPECT_FALSE(poll->frame()->primaryWindow()->input.pointers[1].present);

    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    const auto* first = std::get_if<PointerButtonTransition>(&transitions[0].payload);
    const auto* second = std::get_if<PointerButtonTransition>(&transitions[1].payload);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->pointer, 0);
    EXPECT_EQ(second->pointer, 0);
}

TEST(IosSessionTest, EndedReleasesTheSlotForTheNextFinger)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    EXPECT_TRUE(session->onTouch(0x11, IosTouchPhase::Began, 10.0F, 10.0F));
    EXPECT_TRUE(session->onTouch(0x11, IosTouchPhase::Ended, 20.0F, 30.0F));
    EXPECT_TRUE(session->onTouch(0x12, IosTouchPhase::Began, 40.0F, 50.0F));

    auto poll = session->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->input.pointers[0].present) << "the released slot must serve the next finger";
    EXPECT_DOUBLE_EQ(window->input.pointers[0].logicalX, 40.0);
    EXPECT_FALSE(window->input.pointers[1].present);
}

TEST(IosSessionTest, AMoveWithoutABeganIsRejected)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    EXPECT_FALSE(session->onTouch(0x99, IosTouchPhase::Moved, 10.0F, 10.0F));
    EXPECT_FALSE(session->onTouch(0x99, IosTouchPhase::Ended, 10.0F, 10.0F));
}

TEST(IosSessionTest, UnbindReleasesSlotsAndTheSameIdentityCanBeginAgain)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    EXPECT_TRUE(session->onTouch(0x50, IosTouchPhase::Began, 10.0F, 10.0F));
    ASSERT_NE(pollWindow(*session), nullptr);

    session->unbindLayer();
    EXPECT_FALSE(session->onTouch(0x50, IosTouchPhase::Moved, 20.0F, 20.0F))
        << "UIKit delivers no touchesCancelled: on teardown; the mapping must not survive";

    ASSERT_TRUE(session
                    ->bindLayer(IosNativeLayerHandle{.metalLayer = 0x2000}, FramebufferExtent{2532, 1170},
                                Retina3x)
                    .has_value());
    EXPECT_TRUE(session->onTouch(0x50, IosTouchPhase::Began, 30.0F, 40.0F));

    auto poll = session->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->input.pointers[0].present);
    EXPECT_DOUBLE_EQ(window->input.pointers[0].logicalX, 30.0);
}

TEST(IosSessionTest, TouchesQueuedBeforeTheFirstBindReachTheFirstPoll)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);

    EXPECT_TRUE(session->onTouch(0x50, IosTouchPhase::Began, 15.0F, 25.0F))
        << "the queues outlive the backend so UIKit can deliver a finger before the layer exists";
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    auto poll = session->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.pointers[0].present);
    EXPECT_DOUBLE_EQ(poll->frame()->primaryWindow()->input.pointers[0].logicalX, 15.0);
}

TEST(IosSessionTest, AUtf16CommitWithoutMarkedTextPublishesTextAlone)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    EXPECT_TRUE(session->onTextCommitUtf16(u"hi"));

    auto poll = session->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* text = std::get_if<TextInputTransition>(&transitions[0].payload);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->committedUtf8, "hi");
    EXPECT_EQ(session->facet()->publishedCompositionEndCount(), 0U);
}

TEST(IosSessionTest, ResizeWithoutALayerIsASuccessfulNoOp)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    EXPECT_TRUE(session->resizeDrawable(FramebufferExtent{2532, 1170}, Retina3x).has_value());
}

TEST(IosSessionTest, AResizeDoesNotAdvanceTheBindingRevision)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());
    ASSERT_TRUE(session->pollFrame().has_value());

    const auto before = session->backend()->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());

    ASSERT_TRUE(session->resizeDrawable(FramebufferExtent{2532, 1170}, Retina3x).has_value());

    const auto after = session->backend()->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->framebufferExtent.width, 2532U);
    EXPECT_EQ(after->nativeBindingRevision, before->nativeBindingRevision)
        << "the CAMetalLayer object did not change";
    EXPECT_GT(after->surfaceRevision, before->surfaceRevision);
}

TEST(IosSessionTest, AReplacementLayerAdvancesTheBindingRevision)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());
    ASSERT_TRUE(session->pollFrame().has_value());

    const auto before = session->backend()->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());

    session->unbindLayer();
    ASSERT_TRUE(session
                    ->bindLayer(IosNativeLayerHandle{.metalLayer = 0x2000}, FramebufferExtent{2532, 1170},
                                Retina3x)
                    .has_value());

    const auto after = session->backend()->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_FALSE(after->suspended);
    EXPECT_GT(after->nativeBindingRevision, before->nativeBindingRevision);
}

TEST(IosSessionTest, UnknownHidUsagesAreDroppedAndLettersAreMapped)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    EXPECT_FALSE(session->onKey(HidKeyboardVolumeUp, IosKeyAction::Down, false));
    EXPECT_TRUE(session->onKey(HidKeyboardA, IosKeyAction::Down, false));

    auto poll = session->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.heldKeys.test(static_cast<usize>(Key::A)));
    EXPECT_FALSE(poll->frame()->primaryWindow()->input.heldKeys.test(static_cast<usize>(Key::Unknown)));
    EXPECT_EQ(poll->frame()->inputTransitions().size(), 1U);
}

TEST(IosSessionTest, KeyboardOpsWithoutALayerFailAndPendingIsNone)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);

    EXPECT_EQ(session->pendingSoftKeyboardRequest(), IosSoftKeyboardRequest::None);
    EXPECT_FALSE(session->caretPoints().has_value());

    auto ack = session->acknowledgeSoftKeyboardRequest(IosSoftKeyboardRequest::Show);
    ASSERT_FALSE(ack.has_value());
    EXPECT_EQ(ack.error().code, Core::CoreErrorCode::InvalidArgument);

    auto occlusion = session->onSoftKeyboardOcclusionChanged(100);
    ASSERT_FALSE(occlusion.has_value());
    EXPECT_EQ(occlusion.error().code, Core::CoreErrorCode::InvalidArgument);
}

TEST(IosSessionTest, ShutdownMakesLaterBindsFailAndPollsExit)
{
    auto session = makeSession();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->bindLayer(IosNativeLayerHandle{.metalLayer = InitialLayer}, PortraitExtent, Retina3x)
                    .has_value());

    session->shutdown();
    EXPECT_EQ(session->facet(), nullptr);

    auto status = session->bindLayer(IosNativeLayerHandle{.metalLayer = 0x2000}, PortraitExtent, Retina3x);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_FALSE(session->onTouch(0x50, IosTouchPhase::Began, 1.0F, 1.0F));

    auto poll = session->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->isExitRequested());
}

} // namespace
} // namespace Tina::Platform
