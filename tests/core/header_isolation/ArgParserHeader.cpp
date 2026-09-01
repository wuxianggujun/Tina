#include <tina/core/text/ArgParser.hpp>

#include <optional>
#include <string_view>
#include <type_traits>

// Argument scanning runs before anything else in main, including before the tools install their
// diagnostics. An exception there would surface as a bare terminate with no usage text, so every
// operation is noexcept.
static_assert(noexcept(Tina::Core::ArgScanner(0, nullptr)));
static_assert(noexcept(static_cast<Tina::Core::ArgScanner*>(nullptr)->next()));
static_assert(noexcept(static_cast<Tina::Core::ArgScanner*>(nullptr)->token()));
static_assert(noexcept(static_cast<Tina::Core::ArgScanner*>(nullptr)->flag(std::string_view{})));
static_assert(noexcept(static_cast<Tina::Core::ArgScanner*>(nullptr)->value(std::string_view{})));
static_assert(noexcept(static_cast<Tina::Core::ArgScanner*>(nullptr)->failedOption()));
static_assert(noexcept(static_cast<Tina::Core::ArgScanner*>(nullptr)->failed()));
static_assert(noexcept(Tina::Core::parseArgUnsigned(std::string_view{},
                                                    *static_cast<Tina::Core::u32*>(nullptr))));

// value() must be optional-returning, not string_view-returning. A string_view result would force
// callers back onto "empty means missing", which is the defect this type exists to remove.
static_assert(std::is_same_v<decltype(static_cast<Tina::Core::ArgScanner*>(nullptr)->value(
                                 std::string_view{})),
                             std::optional<std::string_view>>);

// No <windows.h>: this is included by every tool and sample.
#if defined(_WINDOWS_)
#error "ArgParser.hpp must not pull in <windows.h>"
#endif
