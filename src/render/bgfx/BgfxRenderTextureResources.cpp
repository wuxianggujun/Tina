#include "BgfxRenderTextureResources.hpp"

#include <tina/core/base/ScopeExit.hpp>

#include <limits>
#include <new>
#include <stdexcept>

namespace Tina::Render::Bgfx {
namespace {

[[nodiscard]] bgfx::TextureFormat::Enum nativeFormat(RenderTextureFormat format) noexcept
{
    switch (format)
    {
    case RenderTextureFormat::Rgba8Unorm:
    case RenderTextureFormat::Rgba8Srgb: return bgfx::TextureFormat::RGBA8;
    case RenderTextureFormat::Rgba16Float: return bgfx::TextureFormat::RGBA16F;
    case RenderTextureFormat::Rg16Float: return bgfx::TextureFormat::RG16F;
    case RenderTextureFormat::R16Float: return bgfx::TextureFormat::R16F;
    case RenderTextureFormat::Depth24Stencil8: return bgfx::TextureFormat::D24S8;
    case RenderTextureFormat::Depth32Float: return bgfx::TextureFormat::D32F;
    case RenderTextureFormat::Invalid: break;
    }
    return bgfx::TextureFormat::Count;
}

[[nodiscard]] u64 nativeFlags(const RenderTextureDesc& desc) noexcept
{
    u64 flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    if (desc.format == RenderTextureFormat::Rgba8Srgb) flags |= BGFX_TEXTURE_SRGB;
    if (Detail::isDepthRenderTextureFormat(desc.format))
        flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    if (!hasRenderTextureUsage(desc.usage, RenderTextureUsage::Sampled))
        flags |= BGFX_TEXTURE_RT_WRITE_ONLY;
    if (hasRenderTextureUsage(desc.usage, RenderTextureUsage::TransferDestination))
        flags |= BGFX_TEXTURE_BLIT_DST;
    switch (desc.sampleCount)
    {
    case 2: flags |= BGFX_TEXTURE_RT_MSAA_X2; break;
    case 4: flags |= BGFX_TEXTURE_RT_MSAA_X4; break;
    case 8: flags |= BGFX_TEXTURE_RT_MSAA_X8; break;
    case 16: flags |= BGFX_TEXTURE_RT_MSAA_X16; break;
    default: break;
    }
    return flags;
}

} // namespace

Core::Result<GpuRenderTextureId> BgfxRenderTextureResources::create(u32 owner, const RenderTextureDesc& desc)
{
    if (auto status = validateRenderTextureDesc(desc); !status) return Core::failure(std::move(status.error()));
    constexpr usize MaximumRenderTextures = 64;
    const auto* caps = bgfx::getCaps();
    const auto format = nativeFormat(desc.format);
    const u64 flags = nativeFlags(desc);
    if (caps == nullptr || desc.width > caps->limits.maxTextureSize || desc.height > caps->limits.maxTextureSize ||
        !bgfx::isTextureValid(0, false, 1, format, flags))
        return Core::failure(RenderErrorCode::RenderTextureUnsupported,
                             "The renderer cannot create the requested render-target format/usage/sample count");

    try
    {
        usize index = 0;
        for (; index < slots_.size(); ++index)
            if (slots_[index].generation.canReuse(slots_[index].live, slots_[index].retirement != Phase::None)) break;
        if (index == slots_.size())
        {
            if (index >= MaximumRenderTextures || index >= GpuRenderTextureId::InvalidIndex)
                return Core::failure(RenderErrorCode::RenderTextureBindingKeyExhausted, "Render texture slots exhausted");
            slots_.emplace_back();
        }

        std::array<Mip, RenderTextureDesc::MaximumMipCount> candidate{};
        auto rollback = Core::makeScopeExit([&]() noexcept {
            for (auto& mip : candidate)
                if (bgfx::isValid(mip.texture)) bgfx::destroy(mip.texture);
        });
        for (u8 level = 0; level < desc.mipCount; ++level)
        {
            const auto extent = Detail::renderTextureMipExtent(desc, level);
            candidate[level].texture = bgfx::createTexture2D(static_cast<u16>(extent.width),
                static_cast<u16>(extent.height), false, 1, format, flags);
            if (!bgfx::isValid(candidate[level].texture))
                return Core::failure(RenderErrorCode::RenderTextureUnsupported,
                                     "bgfx failed to allocate a render texture mip; all candidate mips rolled back");
        }
        auto& slot = slots_[index];
        slot.desc = desc;
        slot.mips = candidate;
        slot.live = true;
        slot.retirement = Phase::None;
        for (auto& mip : candidate) mip.texture = BGFX_INVALID_HANDLE;
        ++liveCount_;
        nativeCount_ += desc.mipCount;
        return GpuRenderTextureId{owner, static_cast<u32>(index), slot.generation.value()};
    }
    catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return Core::failure(Core::CoreErrorCode::CapacityExceeded); }
}

Core::Status BgfxRenderTextureResources::validate(u32 owner, GpuRenderTextureId id) const noexcept
{
    if (!id || id.owner != owner || id.index >= slots_.size() || !slots_[id.index].live ||
        slots_[id.index].generation.value() != id.generation)
        return Core::failure(RenderErrorCode::RenderTextureNotFound, "Render texture is stale, destroyed or owned by another device");
    return Core::success();
}

Core::Status BgfxRenderTextureResources::bind(u32 owner, u32 key, GpuRenderTextureId id) noexcept
{
    if (key == 0) return Core::failure(RenderErrorCode::InvalidRenderTexture, "Render texture key zero is the output surface");
    if (auto status = validate(owner, id); !status) return status;
    try { bindings_.insert_or_assign(key, id); }
    catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory); }
    return Core::success();
}

Core::Status BgfxRenderTextureResources::clearBinding(u32 key) noexcept
{
    if (key == 0) return Core::failure(RenderErrorCode::InvalidRenderTexture, "Render texture key zero is the output surface");
    bindings_.erase(key);
    return Core::success();
}

std::optional<Detail::RenderTextureResourceView> BgfxRenderTextureResources::resolve(u32 key) const noexcept
{
    const auto binding = bindings_.find(key);
    if (binding == bindings_.end()) return std::nullopt;
    const auto id = binding->second;
    if (id.index >= slots_.size()) return std::nullopt;
    const auto& slot = slots_[id.index];
    if (!slot.live || slot.generation.value() != id.generation) return std::nullopt;
    return Detail::RenderTextureResourceView{id, slot.desc};
}

bgfx::TextureHandle BgfxRenderTextureResources::texture(u32 key, u8 mip) const noexcept
{
    const auto resource = resolve(key);
    return resource && mip < resource->desc.mipCount ? slots_[resource->identity.index].mips[mip].texture
                                                    : bgfx::TextureHandle{bgfx::kInvalidHandle};
}

Core::Result<bgfx::FrameBufferHandle> BgfxRenderTextureResources::framebuffer(u32 colorKey, u8 colorMip,
                                                                           u32 depthKey, u8 depthMip)
{
    const auto color = resolve(colorKey);
    const auto depth = resolve(depthKey);
    if (!color || colorMip >= color->desc.mipCount ||
        (depthKey != 0 && (!depth || depthMip >= depth->desc.mipCount)))
        return Core::failure(RenderErrorCode::InvalidOffscreenPass, "Framebuffer references an invalid render texture mip");
    const auto depthId = depth ? depth->identity : GpuRenderTextureId{};
    for (const auto& entry : framebuffers_)
        if (entry.retirement == Phase::None && bgfx::isValid(entry.handle) &&
            entry.color == color->identity && entry.depth == depthId &&
            entry.colorMip == colorMip && entry.depthMip == depthMip) return entry.handle;

    try
    {
        // Reserve CPU ownership before handing any attachment to bgfx.
        usize index = 0;
        for (; index < framebuffers_.size(); ++index)
            if (!bgfx::isValid(framebuffers_[index].handle)) break;
        if (index == framebuffers_.size()) framebuffers_.emplace_back();
        std::array<bgfx::Attachment, 2> attachments{};
        // Each native image contains a single mip. Attachment::init defaults to
        // AUTO_GEN_MIPS, which bgfx rejects for depth attachments on every backend.
        attachments[0].init(texture(colorKey, colorMip), bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
        if (depth)
            attachments[1].init(texture(depthKey, depthMip), bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
        const u8 count = depth ? 2 : 1;
        const auto handle = bgfx::createFrameBuffer(count, attachments.data(), false);
        if (!bgfx::isValid(handle))
            return Core::failure(RenderErrorCode::InvalidOffscreenPass, "bgfx could not create an offscreen framebuffer");
        framebuffers_[index] = {color->identity, depthId, colorMip, depthMip, handle, Phase::None};
        ++nativeCount_;
        return handle;
    }
    catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return Core::failure(Core::CoreErrorCode::CapacityExceeded); }
}

void BgfxRenderTextureResources::destroyFramebuffer(Framebuffer& entry) noexcept
{
    if (bgfx::isValid(entry.handle))
    {
        bgfx::destroy(entry.handle);
        --nativeCount_;
        ++destroyedCount_;
    }
    entry = {};
}

void BgfxRenderTextureResources::destroySlot(Slot& slot) noexcept
{
    for (auto& mip : slot.mips)
    {
        if (!bgfx::isValid(mip.texture)) continue;
        bgfx::destroy(mip.texture);
        mip.texture = BGFX_INVALID_HANDLE;
        --nativeCount_;
        ++destroyedCount_;
    }
    slot.desc = {};
    slot.retirement = Phase::None;
}

Core::Status BgfxRenderTextureResources::retire(u32 owner, GpuRenderTextureId id, bool deferred) noexcept
{
    if (auto status = validate(owner, id); !status) return status;
    auto& slot = slots_[id.index];
    slot.live = false;
    slot.generation.advanceAfterRelease();
    --liveCount_;
    std::erase_if(bindings_, [id](const auto& entry) { return entry.second == id; });
    for (auto& entry : framebuffers_)
    {
        if (!bgfx::isValid(entry.handle) || (entry.color != id && entry.depth != id)) continue;
        if (!deferred) destroyFramebuffer(entry);
        else if (entry.retirement == Phase::None) entry.retirement = Phase::Queued;
    }
    if (deferred) slot.retirement = Phase::Queued;
    else destroySlot(slot);
    return Core::success();
}

u32 BgfxRenderTextureResources::beginRetirements() noexcept
{
    for (auto& entry : framebuffers_) if (entry.retirement == Phase::Queued) entry.retirement = Phase::Waiting;
    u32 count = 0;
    for (auto& slot : slots_) if (slot.retirement == Phase::Queued) { slot.retirement = Phase::Waiting; ++count; }
    return count;
}

u32 BgfxRenderTextureResources::completeRetirements() noexcept
{
    for (auto& entry : framebuffers_) if (entry.retirement == Phase::Waiting) destroyFramebuffer(entry);
    u32 count = 0;
    for (auto& slot : slots_) if (slot.retirement == Phase::Waiting) { destroySlot(slot); ++count; }
    return count;
}

u64 BgfxRenderTextureResources::pendingRetirementCount() const noexcept
{
    u64 count = 0;
    for (const auto& slot : slots_)
        count += static_cast<u64>(slot.retirement != Phase::None);
    return count;
}

void BgfxRenderTextureResources::shutdown() noexcept
{
    bindings_.clear();
    for (auto& entry : framebuffers_) destroyFramebuffer(entry);
    for (auto& slot : slots_)
    {
        if (slot.live) { slot.live = false; slot.generation.advanceAfterRelease(); }
        destroySlot(slot);
    }
    liveCount_ = 0;
}

} // namespace Tina::Render::Bgfx
