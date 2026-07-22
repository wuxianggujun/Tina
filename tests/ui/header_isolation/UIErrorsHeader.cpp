#include <tina/ui/UIErrors.hpp>

static_assert(
    Tina::UI::UIErrorCode::InvalidContextConfig.domain
    == Tina::Core::ErrorDomain::UI);
static_assert(
    Tina::UI::UIErrorCode::WrongOwnerThread.domain
    == Tina::Core::ErrorDomain::UI);
