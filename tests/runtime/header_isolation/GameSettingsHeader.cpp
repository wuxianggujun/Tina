#include <tina/runtime/GameSettings.hpp>

#include <type_traits>

static_assert(sizeof(Tina::GameSettings) > 0);
static_assert(std::is_same_v<decltype(Tina::writeGameSettingsText(Tina::GameSettings{})),
                             Tina::Core::Result<std::string>>);
static_assert(std::is_same_v<decltype(Tina::parseGameSettingsText("")),
                             Tina::Core::Result<Tina::GameSettings>>);
