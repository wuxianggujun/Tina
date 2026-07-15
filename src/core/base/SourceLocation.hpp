#pragma once

#include <cstdint>
#include <source_location>

namespace Tina::Core {

#if defined(__cpp_lib_source_location)
using SourceLocation = std::source_location;
#else
// Clang 14 with libstdc++ 11 ships <source_location>, but the standard type is
// disabled because that compiler lacks __builtin_source_location. Preserve call
// site diagnostics with the older compiler builtins instead of dropping Clang.
class SourceLocation final {
public:
    static constexpr SourceLocation current(
        const char* file = __builtin_FILE(),
        const char* function = __builtin_FUNCTION(),
        std::uint_least32_t line = __builtin_LINE()) noexcept
    {
        return SourceLocation(file, function, line);
    }

    [[nodiscard]] constexpr const char* file_name() const noexcept { return m_file; }
    [[nodiscard]] constexpr const char* function_name() const noexcept { return m_function; }
    [[nodiscard]] constexpr std::uint_least32_t line() const noexcept { return m_line; }
    [[nodiscard]] constexpr std::uint_least32_t column() const noexcept { return 0; }

private:
    constexpr SourceLocation(const char* file,
                             const char* function,
                             std::uint_least32_t line) noexcept
        : m_file(file), m_function(function), m_line(line)
    {
    }

    const char* m_file = "";
    const char* m_function = "";
    std::uint_least32_t m_line = 0;
};
#endif

} // namespace Tina::Core
