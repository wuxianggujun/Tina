#include <tina/localization/LocalizationErrors.hpp>

static_assert(
    Tina::Localization::LocalizationErrorCode::InvalidData.domain ==
    Tina::Core::ErrorDomain::Localization);
