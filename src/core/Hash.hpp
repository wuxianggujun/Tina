//
// 统一哈希封装：使用 xxHash（XXH3_64bits）
// - 提供对字符串/字节序列的 64 位哈希
// - 提供对 ASCII 字符串小写规范化后的哈希（资源类型名/路径惯用）

#pragma once

#include "Core.hpp"
#include <string>
#include <string_view>
#include <cctype>

// 直接包含xxHash，使用默认函数名（避免命名空间宏导致的问题）
#include "../../thirdparty/xxHash/xxhash.h"

namespace Tina::Core::Hash {

    using Hash64 = Tina::u64;

    inline Hash64 Bytes64(const void* data, size_t size) {
        if (size == 0 || data == nullptr) return 0;
        return (Hash64)XXH3_64bits(data, size);
    }

    inline Hash64 String64(std::string_view s) {
        return Bytes64(s.data(), s.size());
    }

    inline std::string ToLowerASCII(std::string_view s) {
        std::string out; out.resize(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            unsigned char c = (unsigned char)s[i];
            out[i] = (char)std::tolower(c);
        }
        return out;
    }

    inline Hash64 StringLower64(std::string_view s) {
        std::string lower = ToLowerASCII(s);
        return String64(lower);
    }

} // namespace Tina::Core::Hash
