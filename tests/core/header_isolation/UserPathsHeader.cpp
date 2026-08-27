#include <tina/core/io/UserPaths.hpp>

#include <type_traits>

static_assert(std::is_same_v<decltype(Tina::Core::userApplicationDirectory("app")),
                             Tina::Core::Result<std::string>>);
static_assert(std::is_same_v<decltype(Tina::Core::userApplicationFilePath("app", "file")),
                             Tina::Core::Result<std::string>>);
