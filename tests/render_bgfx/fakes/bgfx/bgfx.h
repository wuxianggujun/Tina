#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

// Values copied from bgfx/defines.h rather than invented. The wrap bits in particular
// are a two-bit field per axis, so U_BORDER is U_MIRROR|U_CLAMP; independent bits here
// would let a translation that accidentally ORs two wrap modes pass against the fake
// and then request Border from the real bgfx.
#define BGFX_SAMPLER_U_MIRROR UINT64_C(0x00000001)
#define BGFX_SAMPLER_U_CLAMP UINT64_C(0x00000002)
#define BGFX_SAMPLER_U_BORDER UINT64_C(0x00000003)
#define BGFX_SAMPLER_V_MIRROR UINT64_C(0x00000004)
#define BGFX_SAMPLER_V_CLAMP UINT64_C(0x00000008)
#define BGFX_SAMPLER_V_BORDER UINT64_C(0x0000000c)
#define BGFX_SAMPLER_W_CLAMP UINT64_C(0x00000020)
#define BGFX_SAMPLER_MIN_POINT UINT64_C(0x00000040)
#define BGFX_SAMPLER_MIN_ANISOTROPIC UINT64_C(0x00000080)
#define BGFX_SAMPLER_MAG_POINT UINT64_C(0x00000100)
#define BGFX_SAMPLER_MAG_ANISOTROPIC UINT64_C(0x00000200)
#define BGFX_SAMPLER_MIP_POINT UINT64_C(0x00000400)
#define BGFX_SAMPLER_COMPARE_LEQUAL UINT64_C(0x00020000)
#define BGFX_TEXTURE_RT UINT64_C(0x0000001000000000)
#define BGFX_TEXTURE_SRGB UINT64_C(0x0000200000000000)
#define BGFX_TEXTURE_NONE UINT64_C(0)
#define BGFX_INVALID_HANDLE { tina_test_bgfx::InvalidHandle }

namespace tina_test_bgfx {

struct TextureFormat final {
    // Relative order matches bgfx's enum: the compressed formats precede the
    // uncompressed ones, and Count is last so an unmapped format lands past every
    // real value.
    enum Enum : std::uint8_t {
        BC1 = 1,
        BC3 = 2,
        BC7 = 3,
        ASTC4x4 = 4,
        R8 = 5,
        RG16F = 6,
        RGBA8 = 7,
        RGBA16F = 8,
        D16 = 9,
        Count = 10,
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
    // Mirrors the real struct's writable pointer so callers that fill an alloc'd block
    // in place compile against the fake unchanged.
    std::uint8_t* data = nullptr;
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
    bool cubeMap = false;
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
    bool cubeMap = false;
    std::uint8_t side = 0;
};

struct TextureRecord final {
    TextureHandle handle{};
    bool mutableStorage = false;
    bool destroyed = false;
    std::vector<std::uint8_t> pixels;
};

struct TextureValidationCall final {
    std::uint16_t depth = 0;
    bool cubeMap = false;
    std::uint16_t layers = 0;
    TextureFormat::Enum format = TextureFormat::Count;
    std::uint64_t flags = 0;
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
    std::uint32_t allocCalls = 0;
    std::uint32_t failAllocCall = 0;
    std::uint32_t textureCreateCalls = 0;
    std::uint32_t rejectTextureCreateCall = 0;
    bool rejectTextureCreate = false;
    bool rejectTextureValidation = false;
    // Empty means no per-format rejection. A sentinel drawn from the enum itself would
    // collide with Count, which is what an unmapped format translates to.
    std::optional<TextureFormat::Enum> rejectTextureValidationFormat{};
    bool rejectFrameBufferCreate = false;
    std::uint32_t immutableUpdateRejects = 0;
    std::vector<TextureValidationCall> textureValidations;
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

[[nodiscard]] inline Memory* alloc(std::uint32_t size)
{
    ++Contract::state.allocCalls;
    if (Contract::state.failAllocCall == Contract::state.allocCalls)
    {
        return nullptr;
    }
    auto* memory = new Memory{};
    // Real bgfx::alloc hands back uninitialised storage. Filling with a non-zero
    // pattern means a caller that forgets to write a level leaves this pattern behind
    // instead of the zeros an assertion could mistake for legitimate black pixels.
    memory->bytes.assign(size, 0xCD);
    memory->data = memory->bytes.data();
    return memory;
}

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
    memory->data = memory->bytes.data();
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
    ++Contract::state.textureCreateCalls;
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
    if (!Contract::state.rejectTextureCreate &&
        Contract::state.rejectTextureCreateCall != Contract::state.textureCreateCalls)
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

[[nodiscard]] inline TextureHandle createTextureCube(
    std::uint16_t size,
    bool hasMips,
    std::uint16_t layers,
    TextureFormat::Enum format,
    std::uint64_t flags)
{
    ++Contract::state.textureCreateCalls;
    Contract::TextureCreateCall call{
        .width = size,
        .height = size,
        .hasMips = hasMips,
        .layers = layers,
        .format = format,
        .flags = flags,
        .cubeMap = true,
    };
    Contract::state.textureCreates.push_back(call);

    TextureHandle handle{};
    if (!Contract::state.rejectTextureCreate &&
        Contract::state.rejectTextureCreateCall != Contract::state.textureCreateCalls)
    {
        handle.idx = Contract::state.nextTexture++;
        Contract::state.textures.push_back(Contract::TextureRecord{
            .handle = handle,
            .mutableStorage = true,
        });
    }
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
    std::uint16_t depth,
    bool cubeMap,
    std::uint16_t layers,
    TextureFormat::Enum format,
    std::uint64_t flags) noexcept
{
    Contract::state.textureValidations.push_back(Contract::TextureValidationCall{
        .depth = depth,
        .cubeMap = cubeMap,
        .layers = layers,
        .format = format,
        .flags = flags,
    });
    // A real adapter refuses individual formats rather than every texture at once, so
    // the per-format rejection is modelled separately from the blanket switch.
    if (Contract::state.rejectTextureValidationFormat == format)
    {
        return false;
    }
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

inline void updateTextureCube(
    TextureHandle texture,
    std::uint16_t layer,
    std::uint8_t side,
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
        .cubeMap = true,
        .side = side,
    };
    Contract::state.textureUpdates.push_back(call);

    if (Contract::TextureRecord* record = Contract::findTexture(texture);
        record != nullptr)
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
