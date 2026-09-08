#pragma once
#include <tina/render/RenderPostProcess.hpp>
#include <tina/render/RenderSurface.hpp>
#include <bgfx/bgfx.h>
#include <array>
#include <span>

namespace Tina::Render::Bgfx {

struct BgfxPostProcessDraw final {
    bgfx::ViewId view = 0;
    bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle source = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle auxiliary = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    RenderSurfaceExtent sourceExtent{};
    RenderSurfaceExtent destinationExtent{};
    RenderPipelinePassPlan pass{};
    ToneMappingDesc toneMapping{};
    BloomDesc bloom{};
    FogDesc fog{};
    std::array<float, 16> inverseViewProjection{};
    std::array<float, 16> decalFromWorld{};
    std::array<float, 4> camera{};
    std::array<float, 4> viewport{0, 0, 1, 1};
    std::array<float, 4> decalColor{1, 1, 1, 1};
};

class BgfxPostProcess final {
  public:
    [[nodiscard]] Core::Status initialize();
    void shutdown() noexcept;
    void submit(const BgfxPostProcessDraw& draw) const noexcept;
    [[nodiscard]] bgfx::ShaderHandle vertexShader() const noexcept { return vertex_; }
    [[nodiscard]] std::span<const bgfx::UniformHandle> engineUniforms() const noexcept { return uniforms_; }
    [[nodiscard]] u64 nativeCount() const noexcept;

  private:
    enum Uniform : usize { Source, Auxiliary, SourceInfo, DestinationInfo, Params, Bloom, Fog, FogColor,
                           Camera, Viewport, InverseViewProjection, DecalFromWorld, DecalColor, Count };
    bgfx::ShaderHandle vertex_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle triangle_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::UniformHandle, Count> uniforms_ = [] {
        std::array<bgfx::UniformHandle, Count> result{};
        for (auto& uniform : result) uniform = BGFX_INVALID_HANDLE;
        return result;
    }();
};
} // namespace Tina::Render::Bgfx
