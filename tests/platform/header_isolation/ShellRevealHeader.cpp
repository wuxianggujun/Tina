#include <tina/platform/ShellReveal.hpp>

#include <type_traits>

static_assert(std::has_virtual_destructor_v<Tina::Platform::IShellReveal>);
static_assert(std::is_same_v<
              decltype(&Tina::Platform::IShellReveal::revealPath),
              Tina::Core::Status (Tina::Platform::IShellReveal::*)(std::string_view)>);
