//
// 统一 string_view 类型别名（固定使用 EASTL）
// 你的工程已全面采用 EASTL，因此不提供宏切换，直接导出 eastl::basic_string_view 别名。

#pragma once

#include "Core.hpp"
#include <EASTL/string_view.h>

namespace Tina::Core {
    using string_view = eastl::basic_string_view<char>;
    using wstring_view = eastl::basic_string_view<wchar_t>;
}
