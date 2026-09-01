#include <gtest/gtest.h>

#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderPostProcess.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace Tina::Render {
namespace {

constexpr float Tolerance = 1.0e-5F;
constexpr float Infinity = std::numeric_limits<float>::infinity();
const float QuietNaN = std::numeric_limits<float>::quiet_NaN();

// Binding key 0 always means the primary surface, so every offscreen target in
// these fixtures uses a non-zero key.
constexpr u32 SceneColorKey = 1;
constexpr u32 SceneDepthKey = 2;
constexpr u32 PingKey = 3;
constexpr u32 PongKey = 4;

[[nodiscard]] RenderTextureDesc colorTargetDesc()
{
    return RenderTextureDesc{
        .width = 1920,
        .height = 1080,
        .format = RenderTextureFormat::Rgba16Float,
        .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
        .mipCount = 1,
        .sampleCount = 1,
    };
}

[[nodiscard]] std::vector<RenderPipelinePassKind> kindsOf(const RenderPipelineSchedule& schedule)
{
    std::vector<RenderPipelinePassKind> kinds;
    for (const RenderPipelinePassPlan& pass : schedule.passes()) {
        kinds.push_back(pass.kind);
    }
    return kinds;
}

[[nodiscard]] usize countOf(const RenderPipelineSchedule& schedule, RenderPipelinePassKind kind)
{
    usize count = 0;
    for (const RenderPipelinePassPlan& pass : schedule.passes()) {
        if (pass.kind == kind) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST(RenderTextureDescTest, AcceptsAUsableColorTarget)
{
    EXPECT_TRUE(validateRenderTextureDesc(colorTargetDesc()).has_value());
}

TEST(RenderTextureDescTest, RejectsDegenerateExtentAndFormat)
{
    RenderTextureDesc zeroWidth = colorTargetDesc();
    zeroWidth.width = 0;
    EXPECT_FALSE(validateRenderTextureDesc(zeroWidth).has_value());

    RenderTextureDesc zeroHeight = colorTargetDesc();
    zeroHeight.height = 0;
    EXPECT_FALSE(validateRenderTextureDesc(zeroHeight).has_value());

    RenderTextureDesc invalidFormat = colorTargetDesc();
    invalidFormat.format = RenderTextureFormat::Invalid;
    EXPECT_FALSE(validateRenderTextureDesc(invalidFormat).has_value());

    RenderTextureDesc unknownFormat = colorTargetDesc();
    unknownFormat.format = static_cast<RenderTextureFormat>(0xFFU);
    EXPECT_FALSE(validateRenderTextureDesc(unknownFormat).has_value());

    RenderTextureDesc noUsage = colorTargetDesc();
    noUsage.usage = RenderTextureUsage::None;
    EXPECT_FALSE(validateRenderTextureDesc(noUsage).has_value());
}

// The advertised maximum must be usable, not merely close to failing.
TEST(RenderTextureDescTest, BoundaryDimensionsAndMipCountAreUsable)
{
    RenderTextureDesc atMaximum = colorTargetDesc();
    atMaximum.width = RenderTextureDesc::MaximumDimension;
    atMaximum.height = RenderTextureDesc::MaximumDimension;
    EXPECT_TRUE(validateRenderTextureDesc(atMaximum).has_value());

    RenderTextureDesc pastMaximum = atMaximum;
    pastMaximum.width = RenderTextureDesc::MaximumDimension + 1;
    EXPECT_FALSE(validateRenderTextureDesc(pastMaximum).has_value());

    RenderTextureDesc zeroMips = colorTargetDesc();
    zeroMips.mipCount = 0;
    EXPECT_FALSE(validateRenderTextureDesc(zeroMips).has_value());

    RenderTextureDesc tooManyMips = colorTargetDesc();
    tooManyMips.mipCount = RenderTextureDesc::MaximumMipCount + 1;
    EXPECT_FALSE(validateRenderTextureDesc(tooManyMips).has_value());

    RenderTextureDesc onePixel = colorTargetDesc();
    onePixel.width = 1;
    onePixel.height = 1;
    onePixel.mipCount = 2;
    EXPECT_FALSE(validateRenderTextureDesc(onePixel).has_value());

    RenderTextureDesc nonPowerOfTwo = colorTargetDesc();
    nonPowerOfTwo.width = 3;
    nonPowerOfTwo.height = 1;
    nonPowerOfTwo.mipCount = 2;
    EXPECT_TRUE(validateRenderTextureDesc(nonPowerOfTwo).has_value());
    nonPowerOfTwo.mipCount = 3;
    EXPECT_FALSE(validateRenderTextureDesc(nonPowerOfTwo).has_value());
}

TEST(RenderPostProcessChainTest, AnEmptyChainIsDisabledAndValid)
{
    const RenderPostProcessChainView chain{};
    EXPECT_FALSE(chain.enabled());
    EXPECT_TRUE(validateRenderPostProcessChain(chain).has_value());

    const auto schedule = buildRenderPipelineSchedule(chain, false);
    ASSERT_TRUE(schedule.has_value());
    EXPECT_TRUE(schedule->empty());
}

// Every scene effect writes into the offscreen scene color, so requesting one
// without a target is rejected rather than silently skipped.
TEST(RenderPostProcessChainTest, SceneEffectsRequireAnOffscreenSceneColorTarget)
{
    // Fog opts the chain in; without a scene color target there is nothing for it to
    // write into, so this is rejected rather than quietly skipped.
    RenderPostProcessChainView chain{};
    chain.fog.enabled = true;
    ASSERT_TRUE(chain.enabled());

    const auto rejected = validateRenderPostProcessChain(chain);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, RenderErrorCode::InvalidPostProcessChain);

    chain.sceneColorTargetBindingKey = SceneColorKey;
    EXPECT_TRUE(validateRenderPostProcessChain(chain).has_value());
}

// Tone mapping is a stage of offscreen rendering, not an opt-in of its own. Its
// operator defaults to AcesFitted, so if it counted toward enabled() then every
// RenderFrame -- which carries a default-constructed chain -- would report post
// processing as requested and then fail its own validation.
TEST(RenderPostProcessChainTest, ToneMappingAloneDoesNotEnableTheChain)
{
    RenderPostProcessChainView chain{};
    ASSERT_EQ(chain.toneMapping.operation, ToneMappingOperator::AcesFitted);
    EXPECT_FALSE(chain.enabled());

    chain.toneMapping.operation = ToneMappingOperator::Reinhard;
    EXPECT_FALSE(chain.enabled());

    // Opting in through a scene target makes tone mapping take effect.
    chain.sceneColorTargetBindingKey = SceneColorKey;
    EXPECT_TRUE(chain.enabled());
    const auto schedule = buildRenderPipelineSchedule(chain, false);
    ASSERT_TRUE(schedule.has_value());
    EXPECT_EQ(countOf(*schedule, RenderPipelinePassKind::ToneMapping), 1U);
}

// Bloom ping-pongs between two targets; one target, or the same target twice,
// would read and write the same texture in a single pass.
TEST(RenderPostProcessChainTest, BloomRequiresDistinctPingAndPongTargets)
{
    RenderPostProcessChainView chain{};
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.bloom.enabled = true;

    EXPECT_FALSE(validateRenderPostProcessChain(chain).has_value());

    chain.pingTargetBindingKey = PingKey;
    EXPECT_FALSE(validateRenderPostProcessChain(chain).has_value());

    chain.pongTargetBindingKey = PingKey;
    const auto aliased = validateRenderPostProcessChain(chain);
    ASSERT_FALSE(aliased.has_value());
    EXPECT_EQ(aliased.error().code, RenderErrorCode::InvalidPostProcessChain);

    chain.pongTargetBindingKey = PongKey;
    EXPECT_TRUE(validateRenderPostProcessChain(chain).has_value());
}

TEST(RenderPostProcessChainTest, RejectsNonFiniteAndOutOfRangeParameters)
{
    RenderPostProcessChainView base{};
    base.sceneColorTargetBindingKey = SceneColorKey;
    base.toneMapping.operation = ToneMappingOperator::Reinhard;

    RenderPostProcessChainView zeroExposure = base;
    zeroExposure.toneMapping.exposure = 0.0F;
    EXPECT_FALSE(validateRenderPostProcessChain(zeroExposure).has_value());

    RenderPostProcessChainView nonFiniteGamma = base;
    nonFiniteGamma.toneMapping.outputGamma = QuietNaN;
    EXPECT_FALSE(validateRenderPostProcessChain(nonFiniteGamma).has_value());

    RenderPostProcessChainView badBloom = base;
    badBloom.bloom.enabled = true;
    badBloom.pingTargetBindingKey = PingKey;
    badBloom.pongTargetBindingKey = PongKey;
    badBloom.bloom.downsamplePassCount = 0;
    EXPECT_FALSE(validateRenderPostProcessChain(badBloom).has_value());

    RenderPostProcessChainView invertedFog = base;
    invertedFog.fog.enabled = true;
    invertedFog.fog.linearStart = 100.0F;
    invertedFog.fog.linearEnd = 10.0F;
    EXPECT_FALSE(validateRenderPostProcessChain(invertedFog).has_value());

    RenderPostProcessChainView negativeDensity = base;
    negativeDensity.fog.enabled = true;
    negativeDensity.fog.density = -1.0F;
    EXPECT_FALSE(validateRenderPostProcessChain(negativeDensity).has_value());

    RenderPostProcessChainView unknownToneMapping = base;
    unknownToneMapping.toneMapping.operation = static_cast<ToneMappingOperator>(0xFFU);
    EXPECT_FALSE(validateRenderPostProcessChain(unknownToneMapping).has_value());

    RenderPostProcessChainView unknownFog = base;
    unknownFog.fog.mode = static_cast<FogMode>(0xFFU);
    EXPECT_FALSE(validateRenderPostProcessChain(unknownFog).has_value());
}

TEST(RenderPostProcessChainTest, RejectsInvalidOffscreenPassesAndDecals)
{
    // An offscreen pass needs a stable identity. The scene color target is set here
    // so this fails on the identity alone rather than on the scene-effects rule.
    const std::array<RenderOffscreenPassView, 1> noKey{
        RenderOffscreenPassView{.stablePassKey = 0, .colorTargetBindingKey = PingKey}};
    RenderPostProcessChainView missingIdentity{};
    missingIdentity.sceneColorTargetBindingKey = SceneColorKey;
    missingIdentity.offscreenPasses = noKey;
    const auto rejectedPass = validateRenderPostProcessChain(missingIdentity);
    ASSERT_FALSE(rejectedPass.has_value());
    EXPECT_EQ(rejectedPass.error().code, RenderErrorCode::InvalidOffscreenPass);

    // An offscreen pass must name a color target; 0 would mean the primary surface,
    // which the core pass schedule already owns.
    const std::array<RenderOffscreenPassView, 1> surfaceTarget{
        RenderOffscreenPassView{.stablePassKey = 7, .colorTargetBindingKey = 0}};
    RenderPostProcessChainView targetsSurface{};
    targetsSurface.sceneColorTargetBindingKey = SceneColorKey;
    targetsSurface.offscreenPasses = surfaceTarget;
    const auto rejectedTarget = validateRenderPostProcessChain(targetsSurface);
    ASSERT_FALSE(rejectedTarget.has_value());
    EXPECT_EQ(rejectedTarget.error().code, RenderErrorCode::InvalidOffscreenPass);

    RenderDecal nonFiniteDecal{};
    nonFiniteDecal.materialBindingKey = 5;
    nonFiniteDecal.worldFromDecal[0] = Infinity;
    const std::array<RenderDecal, 1> decals{nonFiniteDecal};
    RenderPostProcessChainView badDecal{};
    badDecal.sceneColorTargetBindingKey = SceneColorKey;
    badDecal.decals = decals;
    const auto rejectedDecal = validateRenderPostProcessChain(badDecal);
    ASSERT_FALSE(rejectedDecal.has_value());
    EXPECT_EQ(rejectedDecal.error().code, RenderErrorCode::InvalidDecal);
}

TEST(RenderPostProcessChainTest, RejectsDepthClearWithoutDepthTarget)
{
    const std::array<RenderOffscreenPassView, 1> passes{
        RenderOffscreenPassView{.stablePassKey = 7,
                               .colorTargetBindingKey = PingKey,
                               .clearDepth = true}};
    RenderPostProcessChainView chain{};
    chain.offscreenPasses = passes;

    const auto rejected = validateRenderPostProcessChain(chain);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, RenderErrorCode::InvalidOffscreenPass);
}

// A CustomShader step without a shader binding could not be executed, and a step
// whose source is its destination would read and write one texture at once.
TEST(RenderPostProcessChainTest, RejectsUnexecutableCustomSteps)
{
    const std::array<RenderPostProcessStep, 1> noShader{RenderPostProcessStep{
        .kind = RenderPostProcessStepKind::CustomShader,
        .sourceBindingKey = SceneColorKey,
        .destinationBindingKey = PingKey,
        .shaderBindingKey = 0,
    }};
    RenderPostProcessChainView missingShader{};
    missingShader.customSteps = noShader;
    EXPECT_FALSE(validateRenderPostProcessChain(missingShader).has_value());

    const std::array<RenderPostProcessStep, 1> aliased{RenderPostProcessStep{
        .kind = RenderPostProcessStepKind::Copy,
        .sourceBindingKey = PingKey,
        .destinationBindingKey = PingKey,
    }};
    RenderPostProcessChainView aliasedStep{};
    aliasedStep.customSteps = aliased;
    EXPECT_FALSE(validateRenderPostProcessChain(aliasedStep).has_value());

    const std::array<RenderPostProcessStep, 1> unknown{RenderPostProcessStep{
        .kind = static_cast<RenderPostProcessStepKind>(0xFFU),
        .sourceBindingKey = SceneColorKey,
        .destinationBindingKey = PingKey,
        .shaderBindingKey = 7,
    }};
    RenderPostProcessChainView unknownStep{};
    unknownStep.customSteps = unknown;
    EXPECT_FALSE(validateRenderPostProcessChain(unknownStep).has_value());
}

TEST(RenderPostProcessChainTest, RejectsChainsBeyondFixedCapacities)
{
    std::vector<RenderDecal> decals(4097);
    for (usize index = 0; index < decals.size(); ++index) {
        decals[index].materialBindingKey = static_cast<u32>(index + 1U);
    }
    RenderPostProcessChainView chain{};
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.decals = decals;

    const auto rejected = validateRenderPostProcessChain(chain);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, RenderErrorCode::PostProcessCapacityExceeded);
}

// The extension schedule owns only offscreen and post work; the core pass enum
// (shadow/Opaque/Transparent/Sprite/UI) stays frozen. This pins the documented
// order so an added effect cannot silently reorder tone mapping after UI.
TEST(RenderPipelineScheduleTest, OrdersEffectsDecalFogBloomToneMappingThenUi)
{
    RenderPostProcessChainView chain{};
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.sceneDepthTargetBindingKey = SceneDepthKey;
    chain.pingTargetBindingKey = PingKey;
    chain.pongTargetBindingKey = PongKey;

    RenderDecal decal{};
    decal.materialBindingKey = 11;
    const std::array<RenderDecal, 1> decals{decal};
    chain.decals = decals;
    chain.fog.enabled = true;
    chain.bloom.enabled = true;
    chain.bloom.downsamplePassCount = 2;
    chain.toneMapping.operation = ToneMappingOperator::AcesFitted;

    const auto schedule = buildRenderPipelineSchedule(chain, true);
    ASSERT_TRUE(schedule.has_value());

    const std::vector<RenderPipelinePassKind> expected{
        RenderPipelinePassKind::Decal,
        RenderPipelinePassKind::Fog,
        RenderPipelinePassKind::BloomPrefilter,
        RenderPipelinePassKind::BloomDownsample,
        RenderPipelinePassKind::BloomDownsample,
        RenderPipelinePassKind::BloomBlur,
        RenderPipelinePassKind::BloomUpsample,
        RenderPipelinePassKind::ToneMapping,
        RenderPipelinePassKind::UIComposite,
    };
    EXPECT_EQ(kindsOf(*schedule), expected);

    // Tone mapping resolves to the primary surface, and UI composites on top.
    const auto passes = schedule->passes();
    EXPECT_EQ(passes[7].destinationBindingKey, 0U);
    EXPECT_EQ(passes[8].destinationBindingKey, 0U);
}

TEST(RenderPipelineScheduleTest, DownsampleCountFollowsTheRequestedPassCount)
{
    RenderPostProcessChainView chain{};
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.pingTargetBindingKey = PingKey;
    chain.pongTargetBindingKey = PongKey;
    chain.bloom.enabled = true;

    for (const u8 requested : {u8{1}, u8{5}, u8{10}}) {
        chain.bloom.downsamplePassCount = requested;
        const auto schedule = buildRenderPipelineSchedule(chain, false);
        ASSERT_TRUE(schedule.has_value()) << "downsamplePassCount " << int{requested};
        EXPECT_EQ(countOf(*schedule, RenderPipelinePassKind::BloomDownsample), requested);
    }
}

// Bloom alternates ping/pong so no downsample step reads the texture it writes.
TEST(RenderPipelineScheduleTest, BloomDownsampleAlternatesTargets)
{
    RenderPostProcessChainView chain{};
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.pingTargetBindingKey = PingKey;
    chain.pongTargetBindingKey = PongKey;
    chain.bloom.enabled = true;
    chain.bloom.downsamplePassCount = 4;

    const auto schedule = buildRenderPipelineSchedule(chain, false);
    ASSERT_TRUE(schedule.has_value());
    for (const RenderPipelinePassPlan& pass : schedule->passes()) {
        if (pass.kind == RenderPipelinePassKind::BloomDownsample) {
            EXPECT_NE(pass.sourceBindingKey, pass.destinationBindingKey);
        }
    }
}

TEST(RenderPipelineScheduleTest, BloomBlurConsumesTheLastDownsampleForOddAndEvenCounts)
{
    RenderPostProcessChainView chain{};
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.pingTargetBindingKey = PingKey;
    chain.pongTargetBindingKey = PongKey;
    chain.bloom.enabled = true;

    for (const u8 requested : {u8{1}, u8{2}, u8{5}}) {
        chain.bloom.downsamplePassCount = requested;
        const auto schedule = buildRenderPipelineSchedule(chain, false);
        ASSERT_TRUE(schedule.has_value()) << "downsamplePassCount " << int{requested};

        const auto passes = schedule->passes();
        const RenderPipelinePassPlan& finalDownsample = passes[requested];
        const RenderPipelinePassPlan& blur = passes[requested + 1U];
        const RenderPipelinePassPlan& upsample = passes[requested + 2U];
        EXPECT_EQ(finalDownsample.kind, RenderPipelinePassKind::BloomDownsample);
        EXPECT_EQ(blur.kind, RenderPipelinePassKind::BloomBlur);
        EXPECT_EQ(blur.sourceBindingKey, finalDownsample.destinationBindingKey);
        EXPECT_NE(blur.sourceBindingKey, blur.destinationBindingKey);
        EXPECT_EQ(upsample.kind, RenderPipelinePassKind::BloomUpsample);
        EXPECT_EQ(upsample.sourceBindingKey, blur.destinationBindingKey);
    }
}

// An offscreen scene target with no effects still has to reach the surface, so the
// schedule inserts a copy rather than leaving the frame blank.
TEST(RenderPipelineScheduleTest, OffscreenSceneWithoutEffectsCopiesToTheSurface)
{
    RenderPostProcessChainView chain{};
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.toneMapping.operation = ToneMappingOperator::None;

    const auto schedule = buildRenderPipelineSchedule(chain, false);
    ASSERT_TRUE(schedule.has_value());
    ASSERT_EQ(schedule->passes().size(), 1U);
    EXPECT_EQ(schedule->passes()[0].kind, RenderPipelinePassKind::Copy);
    EXPECT_EQ(schedule->passes()[0].sourceBindingKey, SceneColorKey);
    EXPECT_EQ(schedule->passes()[0].destinationBindingKey, 0U);
}

TEST(RenderPipelineScheduleTest, StandaloneOffscreenPassDoesNotInventAPrimaryToneMap)
{
    const std::array<RenderOffscreenPassView, 1> offscreen{
        RenderOffscreenPassView{.stablePassKey = 9,
                               .colorTargetBindingKey = PingKey,
                               .clearDepth = false}};
    RenderPostProcessChainView chain{};
    chain.offscreenPasses = offscreen;

    const auto schedule = buildRenderPipelineSchedule(chain, false);
    ASSERT_TRUE(schedule.has_value());
    ASSERT_EQ(schedule->passes().size(), 1U);
    EXPECT_EQ(schedule->passes()[0].kind, RenderPipelinePassKind::OffscreenScene);
    EXPECT_EQ(schedule->passes()[0].destinationBindingKey, PingKey);
}

TEST(RenderPipelineScheduleTest, CustomStepWritingTheSurfaceIsAlreadyFinal)
{
    const std::array<RenderPostProcessStep, 1> steps{RenderPostProcessStep{
        .kind = RenderPostProcessStepKind::Copy,
        .sourceBindingKey = PingKey,
        .destinationBindingKey = 0,
    }};
    RenderPostProcessChainView chain{};
    chain.customSteps = steps;

    const auto schedule = buildRenderPipelineSchedule(chain, false);
    ASSERT_TRUE(schedule.has_value());
    ASSERT_EQ(schedule->passes().size(), 1U);
    EXPECT_EQ(schedule->passes()[0].kind, RenderPipelinePassKind::Copy);
    EXPECT_EQ(schedule->passes()[0].sourceBindingKey, PingKey);
    EXPECT_EQ(schedule->passes()[0].destinationBindingKey, 0U);
}

TEST(RenderPipelineScheduleTest, OffscreenPassesArePlannedBeforeSceneEffects)
{
    const std::array<RenderOffscreenPassView, 2> passes{
        RenderOffscreenPassView{.stablePassKey = 1,
                               .colorTargetBindingKey = PingKey,
                               .clearDepth = false},
        RenderOffscreenPassView{.stablePassKey = 2,
                               .colorTargetBindingKey = PongKey,
                               .clearDepth = false},
    };
    RenderPostProcessChainView chain{};
    chain.offscreenPasses = passes;
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.toneMapping.operation = ToneMappingOperator::Reinhard;

    const auto schedule = buildRenderPipelineSchedule(chain, false);
    ASSERT_TRUE(schedule.has_value());
    ASSERT_GE(schedule->passes().size(), 3U);
    EXPECT_EQ(schedule->passes()[0].kind, RenderPipelinePassKind::OffscreenScene);
    EXPECT_EQ(schedule->passes()[0].itemIndex, 0U);
    EXPECT_EQ(schedule->passes()[1].kind, RenderPipelinePassKind::OffscreenScene);
    EXPECT_EQ(schedule->passes()[1].itemIndex, 1U);
    EXPECT_EQ(schedule->passes()[2].kind, RenderPipelinePassKind::ToneMapping);
}

// The chain-level limits (16 offscreen passes, 16 custom steps, 4096 decals) are
// looser than the 64-pass schedule capacity, so a chain can pass validation and
// still not fit. Decals are the cheapest way to overflow: each one plans a pass.
TEST(RenderPipelineScheduleTest, ScheduleFailsClosedWhenCapacityWouldBeExceeded)
{
    std::vector<RenderDecal> decals(RenderPipelineSchedule::MaximumPassCount + 1U);
    for (usize index = 0; index < decals.size(); ++index) {
        decals[index].materialBindingKey = static_cast<u32>(index + 1U);
    }
    RenderPostProcessChainView chain{};
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.decals = decals;

    // Premise: the chain itself is legal, so this proves the schedule -- not the
    // validator -- is what refuses.
    ASSERT_TRUE(validateRenderPostProcessChain(chain).has_value());
    ASSERT_GT(decals.size(), RenderPipelineSchedule::MaximumPassCount);

    const auto schedule = buildRenderPipelineSchedule(chain, true);
    ASSERT_FALSE(schedule.has_value());
    EXPECT_EQ(schedule.error().code, RenderErrorCode::PostProcessCapacityExceeded);
}

// A chain that fits exactly must still be accepted; the boundary value has to be
// usable rather than merely close to failing.
TEST(RenderPipelineScheduleTest, AChainThatExactlyFillsCapacityIsAccepted)
{
    // MaximumPassCount decals plus the trailing copy to the surface would be one
    // too many, so use one fewer.
    std::vector<RenderDecal> decals(RenderPipelineSchedule::MaximumPassCount - 1U);
    for (usize index = 0; index < decals.size(); ++index) {
        decals[index].materialBindingKey = static_cast<u32>(index + 1U);
    }
    RenderPostProcessChainView chain{};
    chain.sceneColorTargetBindingKey = SceneColorKey;
    chain.decals = decals;

    const auto schedule = buildRenderPipelineSchedule(chain, false);
    ASSERT_TRUE(schedule.has_value());
    EXPECT_EQ(schedule->passes().size(), RenderPipelineSchedule::MaximumPassCount);
    EXPECT_EQ(countOf(*schedule, RenderPipelinePassKind::Decal), decals.size());
}

// An invalid chain must not produce a partial schedule.
TEST(RenderPipelineScheduleTest, InvalidChainYieldsNoSchedule)
{
    RenderPostProcessChainView chain{};
    chain.bloom.enabled = true;
    chain.sceneColorTargetBindingKey = SceneColorKey;
    // Missing ping/pong.
    EXPECT_FALSE(buildRenderPipelineSchedule(chain, false).has_value());
}

TEST(ToneMappingMathTest, NoneIsIdentityApartFromGamma)
{
    const LinearRgba color{0.25F, 0.5F, 0.75F, 0.5F};
    const LinearRgba mapped =
        toneMapLinearColor(color, ToneMappingDesc{.operation = ToneMappingOperator::None,
                                                 .exposure = 1.0F,
                                                 .outputGamma = 1.0F});
    EXPECT_NEAR(mapped.r, color.r, Tolerance);
    EXPECT_NEAR(mapped.g, color.g, Tolerance);
    EXPECT_NEAR(mapped.b, color.b, Tolerance);
    // Alpha is passed through, not tone mapped.
    EXPECT_NEAR(mapped.a, color.a, Tolerance);
}

TEST(ToneMappingMathTest, EveryOperatorIsMonotonicAndBounded)
{
    for (const ToneMappingOperator operation :
         {ToneMappingOperator::None, ToneMappingOperator::Reinhard,
          ToneMappingOperator::AcesFitted, ToneMappingOperator::AgXApproximation}) {
        const ToneMappingDesc desc{.operation = operation, .exposure = 1.0F, .outputGamma = 1.0F};
        float previous = -1.0F;
        for (float input = 0.0F; input <= 8.0F; input += 0.25F) {
            const LinearRgba mapped = toneMapLinearColor(LinearRgba{input, input, input, 1.0F}, desc);
            EXPECT_TRUE(std::isfinite(mapped.r));
            EXPECT_GE(mapped.r, 0.0F);
            EXPECT_GE(mapped.r, previous) << "operator " << static_cast<int>(operation);
            previous = mapped.r;
        }
        // Black stays black under every operator.
        const LinearRgba black = toneMapLinearColor(LinearRgba{0.0F, 0.0F, 0.0F, 1.0F}, desc);
        EXPECT_NEAR(black.r, 0.0F, Tolerance);
    }
}

TEST(ToneMappingMathTest, ExposureScalesInputAndGammaEncodesOutput)
{
    const ToneMappingDesc unitGamma{
        .operation = ToneMappingOperator::Reinhard, .exposure = 1.0F, .outputGamma = 1.0F};
    const ToneMappingDesc doubleExposure{
        .operation = ToneMappingOperator::Reinhard, .exposure = 2.0F, .outputGamma = 1.0F};
    const LinearRgba color{0.25F, 0.25F, 0.25F, 1.0F};
    EXPECT_GT(toneMapLinearColor(color, doubleExposure).r,
              toneMapLinearColor(color, unitGamma).r);

    // Gamma encoding brightens mid tones, so 2.2 must exceed the linear result.
    const ToneMappingDesc encoded{
        .operation = ToneMappingOperator::Reinhard, .exposure = 1.0F, .outputGamma = 2.2F};
    EXPECT_GT(toneMapLinearColor(color, encoded).r, toneMapLinearColor(color, unitGamma).r);
}

// Non-finite or non-positive exposure/gamma fall back to 1 rather than producing
// NaN, because this math also backs headless validation where a NaN would spread.
TEST(ToneMappingMathTest, NonFiniteInputsStayFinite)
{
    const ToneMappingDesc broken{.operation = ToneMappingOperator::AcesFitted,
                                 .exposure = QuietNaN,
                                 .outputGamma = 0.0F};
    const LinearRgba mapped =
        toneMapLinearColor(LinearRgba{QuietNaN, Infinity, -1.0F, QuietNaN}, broken);
    EXPECT_TRUE(std::isfinite(mapped.r));
    EXPECT_TRUE(std::isfinite(mapped.g));
    EXPECT_TRUE(std::isfinite(mapped.b));
    EXPECT_TRUE(std::isfinite(mapped.a));
    EXPECT_GE(mapped.b, 0.0F);
}

TEST(BloomMathTest, BelowThresholdContributesNothing)
{
    const BloomDesc desc{
        .enabled = true, .threshold = 1.0F, .softKnee = 0.5F, .intensity = 1.0F};
    const LinearRgba dark = bloomPrefilterLinearColor(LinearRgba{0.1F, 0.1F, 0.1F, 1.0F}, desc);
    EXPECT_NEAR(dark.r, 0.0F, 1.0e-3F);

    // Well above the threshold the highlight survives.
    const LinearRgba bright = bloomPrefilterLinearColor(LinearRgba{4.0F, 4.0F, 4.0F, 1.0F}, desc);
    EXPECT_GT(bright.r, 0.0F);
}

TEST(BloomMathTest, ContributionRisesMonotonicallyThroughTheKnee)
{
    const BloomDesc desc{
        .enabled = true, .threshold = 1.0F, .softKnee = 0.5F, .intensity = 1.0F};
    float previous = -1.0F;
    for (float input = 0.0F; input <= 4.0F; input += 0.1F) {
        const LinearRgba result =
            bloomPrefilterLinearColor(LinearRgba{input, input, input, 1.0F}, desc);
        EXPECT_TRUE(std::isfinite(result.r));
        EXPECT_GE(result.r, previous - Tolerance) << "input " << input;
        previous = result.r;
    }
}

TEST(BloomMathTest, IntensityScalesTheResultAndZeroSuppressesIt)
{
    const LinearRgba bright{4.0F, 4.0F, 4.0F, 1.0F};
    const BloomDesc weak{
        .enabled = true, .threshold = 1.0F, .softKnee = 0.5F, .intensity = 0.1F};
    const BloomDesc strong{
        .enabled = true, .threshold = 1.0F, .softKnee = 0.5F, .intensity = 1.0F};
    EXPECT_LT(bloomPrefilterLinearColor(bright, weak).r,
              bloomPrefilterLinearColor(bright, strong).r);

    const BloomDesc silent{
        .enabled = true, .threshold = 1.0F, .softKnee = 0.5F, .intensity = 0.0F};
    EXPECT_NEAR(bloomPrefilterLinearColor(bright, silent).r, 0.0F, Tolerance);
}

TEST(BloomMathTest, NonFiniteInputStaysFinite)
{
    const BloomDesc desc{
        .enabled = true, .threshold = 1.0F, .softKnee = 0.5F, .intensity = 1.0F};
    const LinearRgba result =
        bloomPrefilterLinearColor(LinearRgba{QuietNaN, Infinity, -5.0F, 1.0F}, desc);
    EXPECT_TRUE(std::isfinite(result.r));
    EXPECT_TRUE(std::isfinite(result.g));
    EXPECT_TRUE(std::isfinite(result.b));
    EXPECT_GE(result.b, 0.0F);
}

TEST(FogMathTest, DisabledFogIsAnExactIdentity)
{
    const LinearRgba color{0.2F, 0.4F, 0.6F, 0.8F};
    const FogDesc disabled{.enabled = false};
    const LinearRgba result = applyFogToLinearColor(color, 50.0F, disabled);
    EXPECT_FLOAT_EQ(result.r, color.r);
    EXPECT_FLOAT_EQ(result.g, color.g);
    EXPECT_FLOAT_EQ(result.b, color.b);
    EXPECT_FLOAT_EQ(result.a, color.a);
}

// At zero distance nothing is occluded; far away the fog color dominates. Both
// limits must hold for all three modes.
TEST(FogMathTest, EveryModeReachesBothLimits)
{
    const LinearRgba color{1.0F, 1.0F, 1.0F, 1.0F};
    for (const FogMode mode :
         {FogMode::Linear, FogMode::Exponential, FogMode::ExponentialSquared}) {
        FogDesc desc{};
        desc.enabled = true;
        desc.mode = mode;
        desc.colorR = 0.0F;
        desc.colorG = 0.0F;
        desc.colorB = 0.0F;
        desc.density = 0.1F;
        desc.linearStart = 0.0F;
        desc.linearEnd = 100.0F;

        const LinearRgba near = applyFogToLinearColor(color, 0.0F, desc);
        EXPECT_NEAR(near.r, 1.0F, 1.0e-3F) << "mode " << static_cast<int>(mode);

        const LinearRgba far = applyFogToLinearColor(color, 10'000.0F, desc);
        EXPECT_NEAR(far.r, 0.0F, 1.0e-3F) << "mode " << static_cast<int>(mode);
    }
}

TEST(FogMathTest, VisibilityDecreasesWithDistance)
{
    FogDesc desc{};
    desc.enabled = true;
    desc.mode = FogMode::Exponential;
    desc.colorR = 0.0F;
    desc.colorG = 0.0F;
    desc.colorB = 0.0F;
    desc.density = 0.05F;

    const LinearRgba color{1.0F, 1.0F, 1.0F, 1.0F};
    float previous = 2.0F;
    for (float distance = 0.0F; distance <= 200.0F; distance += 10.0F) {
        const LinearRgba result = applyFogToLinearColor(color, distance, desc);
        EXPECT_TRUE(std::isfinite(result.r));
        EXPECT_LE(result.r, previous + Tolerance);
        previous = result.r;
    }
}

TEST(FogMathTest, NonFiniteDistanceAndColorStayFinite)
{
    FogDesc desc{};
    desc.enabled = true;
    desc.mode = FogMode::ExponentialSquared;
    desc.density = 0.02F;

    const LinearRgba result =
        applyFogToLinearColor(LinearRgba{QuietNaN, Infinity, -1.0F, QuietNaN}, QuietNaN, desc);
    EXPECT_TRUE(std::isfinite(result.r));
    EXPECT_TRUE(std::isfinite(result.g));
    EXPECT_TRUE(std::isfinite(result.b));
    EXPECT_TRUE(std::isfinite(result.a));
}

} // namespace Tina::Render
