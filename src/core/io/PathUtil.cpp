#include "PathUtil.hpp"

#if defined(_WIN32)
#include <limits>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Tina::Core::Detail {

#if defined(_WIN32)

bool pathComponentEqualsOrdinal(const std::wstring& left, const std::wstring& right) noexcept
{
    // CompareStringOrdinal takes lengths as int. A component past INT_MAX code units cannot exist
    // on any Windows filesystem, so this is unreachable in practice; report "not equal" rather
    // than truncate the length, which would compare a prefix and could answer equal for two
    // different paths.
    if (left.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        right.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return false;
    }
    return ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                  static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

#endif

} // namespace Tina::Core::Detail
