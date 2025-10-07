//
// Texture2DResource 实现
//

#include "Texture.hpp"
#include "../core/Log.hpp"

#include <bimg/decode.h>
#include <bx/allocator.h>

namespace Tina::Engine {

namespace {
    struct DefaultAllocator : public bx::DefaultAllocator {} s_alloc;
}

bool Texture2DResource::load(const FileSystem::Content& blob)
{
    if (blob.empty()) {
        TINA_ERROR("Texture2D: 空数据: {}", getPath().c_str());
        return false;
    }

    const bimg::ImageContainer* image = bimg::imageParse(&s_alloc, blob.data(), (uint32_t)blob.size());
    if (!image) {
        TINA_ERROR("Texture2D: 解码失败: {}", getPath().c_str());
        return false;
    }

    const bgfx::TextureFormat::Enum fmt = (bgfx::TextureFormat::Enum)image->m_format;
    const bool hasMips = image->m_numMips > 1;
    const uint16_t numLayers = (uint16_t)bx::max<uint32_t>(1u, image->m_numLayers);

    const bgfx::Memory* mem = bgfx::copy(image->m_data, (uint32_t)image->m_size);
    m_tex = bgfx::createTexture2D((uint16_t)image->m_width,
                                  (uint16_t)image->m_height,
                                  hasMips,
                                  numLayers,
                                  fmt,
                                  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                  BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC,
                                  mem);

    m_width = (uint16_t)image->m_width;
    m_height = (uint16_t)image->m_height;

    bimg::imageFree(const_cast<bimg::ImageContainer*>(image));

    if (!bgfx::isValid(m_tex)) {
        TINA_ERROR("Texture2D: 创建纹理失败: {}", getPath().c_str());
        return false;
    }

    TINA_INFO("Texture2D: 加载成功 {} ({}x{})", getPath().c_str(), (int)m_width, (int)m_height);
    return true;
}

void Texture2DResource::unload()
{
    if (bgfx::isValid(m_tex)) {
        bgfx::destroy(m_tex);
        m_tex = BGFX_INVALID_HANDLE;
    }
    m_width = m_height = 0;
}

} // namespace Tina::Engine

