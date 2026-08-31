#include <tina/gameplay/GameplayErrors.hpp>

static_assert(Tina::Gameplay::GameplayErrorCode::ReentrantDispatch.domain ==
              Tina::Core::ErrorDomain::Gameplay);
