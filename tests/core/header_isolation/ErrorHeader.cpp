#include <tina/core/error/Error.hpp>

#include <utility>

static_assert(std::to_underlying(Tina::Core::ErrorDomain::Core) == 1U);
static_assert(std::to_underlying(Tina::Core::ErrorDomain::Cooker) == 12U);
static_assert(std::to_underlying(Tina::Core::ErrorDomain::Animation3D) == 18U);
static_assert(std::to_underlying(Tina::Core::ErrorDomain::AssetFormat) == 19U);
static_assert(Tina::Core::CoreErrorCode::Internal.domain == Tina::Core::ErrorDomain::Core);
