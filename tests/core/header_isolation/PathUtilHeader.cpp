#include "core/io/PathUtil.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>

// This header is included by roughly twenty translation units, so it must not drag <windows.h> in
// with it. The ordinal compare is declared here and defined in PathUtil.cpp for that reason.
#if defined(_WIN32)
#if defined(_WINDOWS_) || defined(CSTR_EQUAL)
#error "PathUtil.hpp must not pull in <windows.h>"
#endif
#endif

// Every predicate is noexcept: they run inside sandbox checks that answer a bool, and several
// callers are themselves noexcept. pathRelativeToAncestor is the deliberate exception because it
// builds a path and can therefore allocate.
static_assert(noexcept(Tina::Core::Detail::pathComponentEquals(std::filesystem::path{},
                                                               std::filesystem::path{})));
static_assert(noexcept(Tina::Core::Detail::pathIsSameOrDescendant(std::filesystem::path{},
                                                                  std::filesystem::path{})));
static_assert(noexcept(Tina::Core::Detail::pathsReferToSameLocation(std::filesystem::path{},
                                                                    std::filesystem::path{})));
static_assert(noexcept(Tina::Core::Detail::pathHasParentComponent(std::filesystem::path{})));
static_assert(noexcept(Tina::Core::Detail::pathEscapesRoot(std::filesystem::path{})));

static_assert(std::is_same_v<decltype(Tina::Core::Detail::pathRelativeToAncestor(
                                 std::filesystem::path{}, std::filesystem::path{})),
                             std::optional<std::filesystem::path>>);

// Both encoders return owned UTF-8 text, never a view into a temporary path.
static_assert(std::is_same_v<decltype(Tina::Core::Detail::pathToUtf8(std::filesystem::path{})),
                             std::string>);
static_assert(
    std::is_same_v<decltype(Tina::Core::Detail::pathToUtf8Generic(std::filesystem::path{})),
                   std::string>);

static_assert(std::is_same_v<decltype(Tina::Core::Detail::pathFromUtf8Bytes(std::string_view{})),
                             std::filesystem::path>);
