#pragma once

#include <tina/core/base/EnumFlags.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/render/UIDisplayList.hpp>

#include <array>
#include <optional>
#include <span>

namespace Tina::Render {

// Public render-target and post-process contracts are intentionally backend
// neutral. A device binding key identifies a live RenderTexture owned by the
// receiving IRenderDevice; key zero always means the primary window surface.
enum class RenderTextureFormat : u8 {
    Invalid = 0,
    Rgba8Unorm = 1,
    Rgba8Srgb = 2,
    Rgba16Float = 3,
    Rg16Float = 4,
    R16Float = 5,
    Depth24Stencil8 = 6,
    Depth32Float = 7,
};

enum class RenderTextureUsage : u16 {
    None = 0,
    ColorAttachment = 1U << 0U,
    DepthStencilAttachment = 1U << 1U,
    Sampled = 1U << 2U,
    TransferSource = 1U << 3U,
    TransferDestination = 1U << 4U,
};

TINA_ENUM_FLAG_OPERATORS(RenderTextureUsage);

[[nodiscard]] constexpr bool hasRenderTextureUsage(RenderTextureUsage value,
                                                   RenderTextureUsage flag) noexcept
{
    return hasAllFlags(value, flag);
}

struct RenderTextureDesc final {
    static constexpr u16 MaximumDimension = 16'384;
    static constexpr u8 MaximumMipCount = 15;

    u16 width = 0;
    u16 height = 0;
    RenderTextureFormat format = RenderTextureFormat::Invalid;
    RenderTextureUsage usage = RenderTextureUsage::None;
    u8 mipCount = 1;
    u8 sampleCount = 1;
};

[[nodiscard]] Core::Status validateRenderTextureDesc(const RenderTextureDesc& desc) noexcept;

enum class ToneMappingOperator : u8 {
    None = 0,
    Reinhard = 1,
    AcesFitted = 2,
    AgXApproximation = 3,
};

struct ToneMappingDesc final {
    ToneMappingOperator operation = ToneMappingOperator::AcesFitted;
    float exposure = 1.0F;
    float outputGamma = 2.2F;
};

struct BloomDesc final {
    bool enabled = false;
    float threshold = 1.0F;
    float softKnee = 0.5F;
    float intensity = 0.08F;
    u8 downsamplePassCount = 5;
};

enum class FogMode : u8 {
    Linear = 0,
    Exponential = 1,
    ExponentialSquared = 2,
};

struct FogDesc final {
    bool enabled = false;
    FogMode mode = FogMode::Exponential;
    float colorR = 0.5F;
    float colorG = 0.55F;
    float colorB = 0.6F;
    float density = 0.02F;
    float linearStart = 10.0F;
    float linearEnd = 100.0F;
};

// Projected box decal. worldFromDecal is column-major and must be finite. The
// unit decal cube [-0.5, 0.5] is projected into the world by this transform.
struct RenderDecal final {
    std::array<float, 16> worldFromDecal{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    u32 materialBindingKey = 0;
    float opacity = 1.0F;
    i32 order = 0;
};

struct RenderOffscreenPassView final {
    u64 stablePassKey = 0;
    u32 colorTargetBindingKey = 0;
    u32 depthTargetBindingKey = 0;
    RenderSceneView scene{};
    UIDisplayListView ui{};
    float clearR = 0.0F;
    float clearG = 0.0F;
    float clearB = 0.0F;
    float clearA = 0.0F;
    bool clearColor = true;
    bool clearDepth = true;
};

enum class RenderPostProcessStepKind : u8 {
    Copy = 0,
    CustomShader = 1,
};

struct RenderPostProcessStep final {
    RenderPostProcessStepKind kind = RenderPostProcessStepKind::Copy;
    u32 sourceBindingKey = 0;
    u32 destinationBindingKey = 0;
    // Required by CustomShader and ignored by Copy.
    u32 shaderBindingKey = 0;
};

struct RenderPostProcessChainView final {
    // Non-zero opts the primary world scene into offscreen HDR rendering. The
    // texture must be color-attachable and sampled. ping/pong are required by
    // bloom and by custom steps which do not target the primary surface.
    u32 sceneColorTargetBindingKey = 0;
    u32 sceneDepthTargetBindingKey = 0;
    u32 pingTargetBindingKey = 0;
    u32 pongTargetBindingKey = 0;
    std::span<const RenderOffscreenPassView> offscreenPasses{};
    std::span<const RenderDecal> decals{};
    FogDesc fog{};
    BloomDesc bloom{};
    ToneMappingDesc toneMapping{};
    std::span<const RenderPostProcessStep> customSteps{};

    // True only when the caller actually asked for offscreen or post work.
    //
    // Tone mapping is deliberately NOT part of this test even though its operator
    // defaults to AcesFitted. A default-constructed chain is what every RenderFrame
    // carries when the caller never mentions post processing, so counting the
    // default operator here would report that frame as enabled -- and then fail its
    // own validation, because tone mapping requires a scene color target that the
    // caller never set. Tone mapping is a stage OF offscreen rendering, so it only
    // takes effect once something else opts in.
    [[nodiscard]] constexpr bool enabled() const noexcept
    {
        return sceneColorTargetBindingKey != 0 || !offscreenPasses.empty() ||
               !decals.empty() || fog.enabled || bloom.enabled ||
               !customSteps.empty();
    }
};

[[nodiscard]] Core::Status
validateRenderPostProcessChain(const RenderPostProcessChainView& chain) noexcept;

// Core scene passes remain owned by RenderPassScheduler. This extension schedule
// describes only offscreen and post work, so adding an effect never changes the
// frozen core pass enum or its default schedule.
enum class RenderPipelinePassKind : u8 {
    OffscreenScene = 0,
    Decal = 1,
    Fog = 2,
    BloomPrefilter = 3,
    BloomDownsample = 4,
    BloomBlur = 5,
    BloomUpsample = 6,
    ToneMapping = 7,
    Copy = 8,
    CustomShader = 9,
    UIComposite = 10,
};

struct RenderPipelinePassPlan final {
    RenderPipelinePassKind kind = RenderPipelinePassKind::Copy;
    u32 sourceBindingKey = 0;
    u32 destinationBindingKey = 0;
    u32 auxiliaryBindingKey = 0;
    u32 itemIndex = 0;
    u32 iteration = 0;
    bool clearColor = false;
    bool clearDepth = false;
};

class RenderPipelineSchedule final {
  public:
    static constexpr u32 MaximumPassCount = 64;

    [[nodiscard]] constexpr std::span<const RenderPipelinePassPlan> passes() const noexcept
    {
        return {m_passes.data(), m_passCount};
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_passCount == 0; }

  private:
    friend Core::Result<RenderPipelineSchedule>
    buildRenderPipelineSchedule(const RenderPostProcessChainView&, bool) noexcept;

    std::array<RenderPipelinePassPlan, MaximumPassCount> m_passes{};
    u32 m_passCount = 0;
};

[[nodiscard]] Core::Result<RenderPipelineSchedule>
buildRenderPipelineSchedule(const RenderPostProcessChainView& chain,
                            bool hasPrimaryWindowUI) noexcept;

// Scalar reference math shared by Null/headless validation, tools and shader
// conformance fixtures. Inputs are scene-linear and outputs stay finite.
struct LinearRgba final {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

[[nodiscard]] LinearRgba toneMapLinearColor(LinearRgba color,
                                            const ToneMappingDesc& desc) noexcept;
[[nodiscard]] LinearRgba bloomPrefilterLinearColor(LinearRgba color,
                                                   const BloomDesc& desc) noexcept;
[[nodiscard]] LinearRgba applyFogToLinearColor(LinearRgba color, float cameraDistance,
                                               const FogDesc& desc) noexcept;

} // namespace Tina::Render
