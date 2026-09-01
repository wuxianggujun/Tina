#include <tina/core/io/ApplicationPaths.hpp>

#include <type_traits>

static_assert(std::is_same_v<decltype(Tina::Core::applicationDirectory()),
                             Tina::Core::Result<std::string>>);
static_assert(std::is_same_v<decltype(Tina::Core::applicationFilePath("assets/pack.tinapack")),
                             Tina::Core::Result<std::string>>);
