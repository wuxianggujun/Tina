#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#define BGFX_SAMPLER_U_CLAMP UINT64_C(0x00000001)
#define BGFX_SAMPLER_V_CLAMP UINT64_C(0x00000002)
#define BGFX_SAMPLER_MIN_POINT UINT64_C(0x00000004)
#define BGFX_SAMPLER_MAG_POINT UINT64_C(0x00000008)
#define BGFX_SAMPLER_COMPARE_LEQUAL UINT64_C(0x00000010)
#define BGFX_TEXTURE_RT UINT64_C(0x00000100)
#define BGFX_INVALID_HANDLE { tina_test_bgfx::InvalidHandle }

namespace tina_test_bgfx {

struct TextureFormat final {
    enum Enum : std::uint8_t {
        R8 = 1,
        D16 = 2,
    };
};

inline constexpr std::uint16_t InvalidHandle = (std::numeric_limits<std::uint16_t>::max)();

struct TextureHandle final {
    std::uint16_t idx = InvalidHandle;
};

struct FrameBufferHandle final {
    std::uint16_t idx = InvalidHandle;
};

struct Memory final {
    std::vector<std::uint8_t> bytes;
};

namespace Contract {

struct TextureCreateCall final {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    bool hasMips = false;
    std::uint16_t layers = 0;
    TextureFormat::Enum format = TextureFormat::R8;
    std::uint64_t flags = 0;
    bool initialMemoryProvided = false;
    std::vector<std::uint8_t> initialPixels;
};

struct TextureUpdateCall final {
    TextureHandle texture{};
    std::uint16_t layer = 0;
    std::uint8_t mip = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<std::uint8_t> pixels;
};

struct TextureRecord final {
    TextureHandle handle{};
    bool mutableStorage = false;
    bool destroyed = false;
    std::vector<std::uint8_t> pixels;
};

struct FrameBufferCreateCall final {
    std::vector<TextureHandle> attachments;
    bool destroyTextures = false;
};

enum class DestroyedResourceKind : std::uint8_t {
    Texture,
    FrameBuffer,
};

struct DestroyedResource final {
    DestroyedResourceKind kind = DestroyedResourceKind::Texture;
    std::uint16_t index = InvalidHandle;
};

struct State final {
    std::uint16_t nextTexture = 1;
    std::uint16_t nextFrameBuffer = 1;
    std::uint32_t copyCalls = 0;
    std::uint32_t failCopyCall = 0;
    bool rejectTextureCreate = false;
    bool rejectTextureValidation = false;
    bool rejectFrameBufferCreate = false;
    std::uint32_t immutableUpdateRejects = 0;
    std::vector<TextureCreateCall> textureCreates;
    std::vector<TextureUpdateCall> textureUpdates;
    std::vector<FrameBufferCreateCall> frameBufferCreates;
    std::vector<TextureHandle> textureDestroys;
    std::vector<FrameBufferHandle> frameBufferDestroys;
    std::vector<DestroyedResource> destroyedResources;
    std::vector<TextureRecord> textures;
};

inline State state{};

inline void reset() noexcept
{
    state = State{};
}

[[nodiscard]] inline TextureRecord* findTexture(TextureHandle handle) noexcept
{
    for (auto& texture : state.textures)
    {
        if (texture.handle.idx == handle.idx)
        {
            return &texture;
        }
    }
    return nullptr;
}

} // namespace Contract

[[nodiscard]] inline const Memory* copy(const void* data, std::uint32_t size)
{
    ++Contract::state.copyCalls;
    if (Contract::state.failCopyCall == Contract::state.copyCalls)
    {
        return nullptr;
    }

    auto* memory = new Memory{};
    memory->bytes.resize(size);
    if (size != 0)
    {
        std::memcpy(memory->bytes.data(), data, size);
    }
    return memory;
}

[[nodiscard]] inline TextureHandle createTexture2D(
    std::uint16_t width,
    std::uint16_t height,
    bool hasMips,
    std::uint16_t layers,
    TextureFormat::Enum format,
    std::uint64_t flags,
    const Memory* memory)
{
    Contract::TextureCreateCall call{
        .width = width,
        .height = height,
        .hasMips = hasMips,
        .layers = layers,
        .format = format,
        .flags = flags,
        .initialMemoryProvided = memory != nullptr,
        .initialPixels = memory != nullptr ? memory->bytes : std::vector<std::uint8_t>{},
    };
    Contract::state.textureCreates.push_back(call);

    TextureHandle handle{};
    if (!Contract::state.rejectTextureCreate)
    {
        handle.idx = Contract::state.nextTexture++;
        Contract::state.textures.push_back(Contract::TextureRecord{
            .handle = handle,
            .mutableStorage = memory == nullptr,
            .pixels = call.initialPixels,
        });
    }

    delete memory;
    return handle;
}

[[nodiscard]] inline bool isValid(TextureHandle texture) noexcept
{
    return texture.idx != InvalidHandle;
}

[[nodiscard]] inline bool isValid(FrameBufferHandle frameBuffer) noexcept
{
    return frameBuffer.idx != InvalidHandle;
}

[[nodiscard]] inline bool isTextureValid(
    std::uint16_t,
    bool,
    std::uint16_t,
    TextureFormat::Enum,
    std::uint64_t) noexcept
{
    return !Contract::state.rejectTextureValidation;
}

[[nodiscard]] inline FrameBufferHandle createFrameBuffer(
    std::uint8_t attachmentCount,
    const TextureHandle* attachments,
    bool destroyTextures)
{
    Contract::state.frameBufferCreates.push_back(Contract::FrameBufferCreateCall{
        .attachments = std::vector<TextureHandle>(attachments, attachments + attachmentCount),
        .destroyTextures = destroyTextures,
    });
    if (Contract::state.rejectFrameBufferCreate)
    {
        return {};
    }
    return FrameBufferHandle{Contract::state.nextFrameBuffer++};
}

inline void updateTexture2D(
    TextureHandle texture,
    std::uint16_t layer,
    std::uint8_t mip,
    std::uint16_t x,
    std::uint16_t y,
    std::uint16_t width,
    std::uint16_t height,
    const Memory* memory)
{
    Contract::TextureUpdateCall call{
        .texture = texture,
        .layer = layer,
        .mip = mip,
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .pixels = memory != nullptr ? memory->bytes : std::vector<std::uint8_t>{},
    };
    Contract::state.textureUpdates.push_back(call);

    if (Contract::TextureRecord* record = Contract::findTexture(texture); record != nullptr)
    {
        if (record->mutableStorage && !record->destroyed)
        {
            record->pixels = call.pixels;
        }
        else
        {
            ++Contract::state.immutableUpdateRejects;
        }
    }

    delete memory;
}

inline void destroy(TextureHandle texture) noexcept
{
    Contract::state.textureDestroys.push_back(texture);
    Contract::state.destroyedResources.push_back(
        {.kind = Contract::DestroyedResourceKind::Texture, .index = texture.idx});
    if (Contract::TextureRecord* record = Contract::findTexture(texture); record != nullptr)
    {
        record->destroyed = true;
    }
}

inline void destroy(FrameBufferHandle frameBuffer) noexcept
{
    Contract::state.frameBufferDestroys.push_back(frameBuffer);
    Contract::state.destroyedResources.push_back(
        {.kind = Contract::DestroyedResourceKind::FrameBuffer, .index = frameBuffer.idx});
}

} // namespace tina_test_bgfx
