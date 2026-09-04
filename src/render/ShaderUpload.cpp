#include <tina/render/RenderDevice.hpp>

#include <array>
#include <cmath>
#include <string_view>

namespace Tina::Render {
namespace {

// Both Sprite2D and Mesh3D are supported: each has a contract .sh header and engine vertex shader
// that custom fragment stages link against. The backend's createShader case list must match this
// set: accepting a kind here but having no program linker is headless-green/real-backend-red split.
[[nodiscard]] constexpr bool isSupportedShaderKind(GpuShaderKind kind) noexcept
{
    switch (kind)
    {
    case GpuShaderKind::Sprite2D:
    case GpuShaderKind::Mesh3D:
        return true;
    case GpuShaderKind::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] constexpr bool isSupportedShaderBinaryProfile(GpuShaderBinaryProfile profile) noexcept
{
    switch (profile)
    {
    case GpuShaderBinaryProfile::Glsl120:
    case GpuShaderBinaryProfile::SpirV:
    case GpuShaderBinaryProfile::Dxbc50:
    case GpuShaderBinaryProfile::Essl300:
        return true;
    case GpuShaderBinaryProfile::Invalid:
        break;
    }
    return false;
}

// Length up to the terminator. A table entry that fills every byte without one is treated as
// unterminated, which the caller reports as an empty name rather than reading past the array.
[[nodiscard]] constexpr usize
nameByteLength(const std::array<char, GpuShaderUniformValue::MaximumNameBytes + 1>& name) noexcept
{
    for (usize index = 0; index < name.size(); ++index)
    {
        if (name[index] == '\0')
        {
            return index;
        }
    }
    return 0;
}

} // namespace

Core::Status validateShaderUploadDesc(const GpuShaderUploadDesc& desc) noexcept
{
    if (!isSupportedShaderKind(desc.shaderKind))
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Shader upload kind is not a supported engine program");
    }
    if (desc.binaries.empty() || desc.binaries.size() > GpuShaderUploadDesc::MaximumBinaryCount)
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Shader upload must carry between one and MaximumBinaryCount binaries");
    }

    auto previousProfile = GpuShaderBinaryProfile::Invalid;
    for (const GpuShaderBinary& binary : desc.binaries)
    {
        if (!isSupportedShaderBinaryProfile(binary.profile))
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader upload carries an unsupported renderer profile");
        }
        // Strictly ascending, so the table has no duplicate profile for the backend to choose
        // between and can be searched in the order given.
        if (static_cast<u8>(binary.profile) <= static_cast<u8>(previousProfile))
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader upload binaries must be sorted by ascending profile");
        }
        previousProfile = binary.profile;
        if (binary.bytes.empty())
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader upload binaries must not be empty");
        }
    }
    return Core::success();
}

Core::Status validateShaderUniformBindingDesc(const GpuShaderUniformBindingDesc& desc) noexcept
{
    if (desc.values.size() > GpuShaderUniformBindingDesc::MaximumValueCount)
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Shader uniform binding carries more values than MaximumValueCount");
    }
    for (usize index = 0; index < desc.values.size(); ++index)
    {
        const GpuShaderUniformValue& entry = desc.values[index];
        const usize nameLength = nameByteLength(entry.name);
        if (nameLength == 0)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader uniform binding carries a value with an empty name");
        }
        for (const float component : entry.value)
        {
            if (!std::isfinite(component))
            {
                return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                     "Shader uniform binding values must be finite");
            }
        }
        // Quadratic, but MaximumValueCount is 16: 120 comparisons at bind time is cheaper than a
        // hash set allocation in a noexcept path.
        for (usize other = 0; other < index; ++other)
        {
            if (std::string_view{desc.values[other].name.data(), nameByteLength(desc.values[other].name)} ==
                std::string_view{entry.name.data(), nameLength})
            {
                return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                     "Shader uniform binding names must be unique");
            }
        }
    }
    return Core::success();
}

Core::Status validateShaderTextureBindingDesc(const GpuShaderTextureBindingDesc& desc) noexcept
{
    if (desc.values.size() > GpuShaderTextureBindingDesc::MaximumValueCount)
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Shader texture binding carries more textures than MaximumValueCount");
    }
    for (usize index = 0; index < desc.values.size(); ++index)
    {
        const GpuShaderTextureValue& entry = desc.values[index];
        const usize nameLength = nameByteLength(entry.name);
        if (nameLength == 0)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader texture binding carries a texture with an empty name");
        }
        // Only the shape of the id, not whether it is live: liveness is device state, so the backend's
        // own texture table is what decides it. A default-constructed id is caller error either way.
        if (!entry.texture)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader texture binding carries an invalid texture id");
        }
        // Quadratic like the value table above, over an even smaller maximum.
        for (usize other = 0; other < index; ++other)
        {
            if (std::string_view{desc.values[other].name.data(), nameByteLength(desc.values[other].name)} ==
                std::string_view{entry.name.data(), nameLength})
            {
                return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                     "Shader texture binding names must be unique");
            }
        }
    }
    return Core::success();
}

} // namespace Tina::Render
