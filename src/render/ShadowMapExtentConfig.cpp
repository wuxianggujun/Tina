#include <tina/render/ShadowMapExtentConfig.hpp>

#include <tina/render/RenderErrors.hpp>

#include <bit>
#include <string_view>
#include <utility>

namespace Tina::Render {
namespace {

[[nodiscard]] bool isValidShadowMapExtent(u16 extent) noexcept
{
    return extent >= ShadowMapExtentConfig::MinimumExtent &&
           extent <= ShadowMapExtentConfig::MaximumExtent &&
           std::has_single_bit(extent);
}

[[nodiscard]] Core::Status invalidExtent(std::string_view fieldName)
{
    Core::Error error{
        RenderErrorCode::InvalidShadowMapExtentConfig,
        "Shadow-map extents must be powers of two in the inclusive range [128, 4096]",
    };
    error.addContext("validateShadowMapExtentConfig", fieldName);
    return Core::failure(std::move(error));
}

} // namespace

Core::Status validateShadowMapExtentConfig(const ShadowMapExtentConfig& config)
{
    if (!isValidShadowMapExtent(config.directionalCascadeTileExtent))
    {
        return invalidExtent("directionalCascadeTileExtent");
    }
    if (!isValidShadowMapExtent(config.spotLightMapExtent))
    {
        return invalidExtent("spotLightMapExtent");
    }
    if (!isValidShadowMapExtent(config.pointLightFaceExtent))
    {
        return invalidExtent("pointLightFaceExtent");
    }
    return Core::success();
}

} // namespace Tina::Render
