#include <gtest/gtest.h>

#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/RuntimeErrors.hpp>

#include <limits>

// EngineConfig::validate() is the only gate between a caller's startup config and
// an engine that has already begun allocating fixed-capacity storage against it.
// Every branch below is fail-closed by design, and none of them had a test: a
// regression that let one through would surface as a capacity failure or an
// undefined backend state much later, with nothing pointing back at the config.
//
// Each case starts from Defaults() and changes exactly one field, so a failure
// names the field rather than "the config is invalid".
namespace Tina::Tests {
namespace {

[[nodiscard]] EngineConfig defaults()
{
    return EngineConfig::Defaults();
}

void expectRejected(const EngineConfig& config, const char* what)
{
    const Core::Status status = config.validate();
    EXPECT_FALSE(status) << "validate() accepted " << what;
    if (!status)
    {
        EXPECT_EQ(status.error().code, ConfigurationErrorCode::InvalidEngineConfig)
            << what << " was rejected with an unexpected code";
    }
}

// The baseline has to pass, or every rejection below proves nothing.
TEST(EngineConfigValidationTest, DefaultsAreValid)
{
    const Core::Status status = defaults().validate();
    EXPECT_TRUE(status) << (status ? "" : status.error().message);
}

TEST(EngineConfigValidationTest, RejectsAnInvalidPrimaryWindowMode)
{
    EngineConfig config = defaults();
    // Only Windowed and BorderlessFullscreen exist; anything else would reach the
    // platform backend as an unhandled enum.
    config.primaryWindow.mode = static_cast<Platform::WindowMode>(0x7F);
    expectRejected(config, "an out-of-range primaryWindow.mode");
}

// Draw call capacity is not a free-form number: the backend allocates in
// 1024-entry blocks, so a value between blocks would silently round and leave the
// caller's expectation wrong.
TEST(EngineConfigValidationTest, RejectsADrawCallCapacityThatIsNotABlockOrTheNativeMaximum)
{
    EngineConfig config = defaults();
    config.renderDrawCallCapacity = 0;
    expectRejected(config, "a zero renderDrawCallCapacity");

    config = defaults();
    config.renderDrawCallCapacity = 1000;
    expectRejected(config, "a renderDrawCallCapacity that is not a 1024 block");

    config = defaults();
    config.renderDrawCallCapacity = 1025;
    expectRejected(config, "a renderDrawCallCapacity just past a block boundary");

    // The two accepted shapes: an exact block, and the native maximum.
    config = defaults();
    config.renderDrawCallCapacity = 1024;
    EXPECT_TRUE(config.validate());
    config.renderDrawCallCapacity = 65535;
    EXPECT_TRUE(config.validate());
}

// MSAA reaches the backbuffer directly, so an unsupported count is a device-create
// failure rather than a degraded image.
TEST(EngineConfigValidationTest, RejectsMsaaSampleCountsTheBackbufferCannotUse)
{
    for (const Core::u8 rejected : {Core::u8{1}, Core::u8{3}, Core::u8{6}, Core::u8{32}})
    {
        EngineConfig config = defaults();
        config.renderMsaaSamples = rejected;
        expectRejected(config, "an unsupported renderMsaaSamples value");
    }

    // 0 means off; the rest are the power-of-two counts the backend supports.
    for (const Core::u8 accepted : {Core::u8{0}, Core::u8{2}, Core::u8{4}, Core::u8{8}, Core::u8{16}})
    {
        EngineConfig config = defaults();
        config.renderMsaaSamples = accepted;
        const Core::Status status = config.validate();
        EXPECT_TRUE(status) << "renderMsaaSamples=" << static_cast<unsigned>(accepted)
                            << " was rejected: " << (status ? "" : status.error().message);
    }
}

TEST(EngineConfigValidationTest, RejectsAPlatformEventSubscriberCapacityOutsideTheSupportedRange)
{
    EngineConfig config = defaults();
    // Zero subscribers cannot deliver the events the engine itself relies on.
    config.platformEventSubscriptions.subscriberCapacity = 0;
    expectRejected(config, "a zero platform event subscriber capacity");

    config = defaults();
    config.platformEventSubscriptions.subscriberCapacity =
        PlatformEventSubscriptionConfig::MaximumSubscriberCapacity + 1U;
    expectRejected(config, "a platform event subscriber capacity above the maximum");

    // The boundary itself is valid, which is the half of a range check that is
    // easiest to get wrong.
    config = defaults();
    config.platformEventSubscriptions.subscriberCapacity =
        PlatformEventSubscriptionConfig::MaximumSubscriberCapacity;
    EXPECT_TRUE(config.validate());
}

// Time scale multiplies the simulation step. A negative value would run the
// simulation backwards and NaN would poison every derived duration.
TEST(EngineConfigValidationTest, RejectsANonFiniteOrNegativeGameplayTimeScale)
{
    EngineConfig config = defaults();
    config.gameplayTimeScale = -0.5;
    expectRejected(config, "a negative gameplayTimeScale");

    config = defaults();
    config.gameplayTimeScale = std::numeric_limits<double>::quiet_NaN();
    expectRejected(config, "a NaN gameplayTimeScale");

    config = defaults();
    config.gameplayTimeScale = std::numeric_limits<double>::infinity();
    expectRejected(config, "an infinite gameplayTimeScale");

    // Zero is legal: it is how a game pauses simulation without stopping the loop.
    config = defaults();
    config.gameplayTimeScale = 0.0;
    EXPECT_TRUE(config.validate());
}

// The shutdown deadline bounds how long EngineHost waits for task workers to
// exit. Zero or negative would make shutdown either instant-fail or undefined,
// and this is the value that decides whether a hung worker is reported or hangs
// the process.
TEST(EngineConfigValidationTest, RejectsANonPositiveOrNonFiniteShutdownDeadline)
{
    EngineConfig config = defaults();
    config.shutdownDeadline = Core::Duration{0.0};
    expectRejected(config, "a zero shutdownDeadline");

    config = defaults();
    config.shutdownDeadline = Core::Duration{-1.0};
    expectRejected(config, "a negative shutdownDeadline");

    config = defaults();
    config.shutdownDeadline = Core::Duration{std::numeric_limits<double>::quiet_NaN()};
    expectRejected(config, "a NaN shutdownDeadline");
}

// The step cap bounds catch-up work after a long frame. Exceeding it would let a
// single frame run an unbounded number of simulation steps, which is the classic
// death spiral.
TEST(EngineConfigValidationTest, RejectsMoreFixedStepsPerFrameThanTheCap)
{
    EngineConfig config = defaults();
    config.fixedSimulation.maximumStepsPerFrame = EngineConfig::MaximumFixedStepsPerFrame + 1U;
    expectRejected(config, "a maximumStepsPerFrame above the cap");

    config = defaults();
    config.fixedSimulation.maximumStepsPerFrame = EngineConfig::MaximumFixedStepsPerFrame;
    EXPECT_TRUE(config.validate());
}

// Delegated validators must not be bypassed: validate() is the single entry point
// callers use, so a nested capacity error has to surface through it.
TEST(EngineConfigValidationTest, SurfacesFailuresFromDelegatedCapacityValidators)
{
    EngineConfig config = defaults();
    config.renderSceneCapacities.spriteCapacity = 0;
    expectRejected(config, "a zero render sprite capacity");

    config = defaults();
    config.primaryWindowUICapacities.nodeCapacity = 0;
    expectRejected(config, "a zero UI node capacity");

    config = defaults();
    config.primaryWindowUIDisplayListCapacities.commandCapacity = 0;
    expectRejected(config, "a zero UI display list command capacity");

    config = defaults();
    config.primaryWindowUIDisplayListCapacities.commandCapacity =
        PrimaryWindowUIDisplayListCapacityConfig::MaximumEntryCapacity + 1U;
    expectRejected(config, "a UI display list command capacity above the maximum");

    config = defaults();
    config.platformFrameCapacities.inputTransitionCapacity = 0;
    expectRejected(config, "a zero platform input transition capacity");
}

// A nested validator's message must reach the caller, or diagnosing a rejected
// config means guessing which of the delegated validators refused it.
TEST(EngineConfigValidationTest, KeepsTheNestedValidatorMessageAsContext)
{
    EngineConfig config = defaults();
    config.renderSceneCapacities.spriteCapacity = 0;
    const Core::Status status = config.validate();
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ConfigurationErrorCode::InvalidEngineConfig);
    EXPECT_NE(status.error().message.find("renderSceneCapacities"), std::string::npos)
        << "the outer message should name the offending field; got: " << status.error().message;
    EXPECT_FALSE(status.error().context.empty())
        << "the nested validator's reason was dropped instead of being attached";
}

} // namespace
} // namespace Tina::Tests
