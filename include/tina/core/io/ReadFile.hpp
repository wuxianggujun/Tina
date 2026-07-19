#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <memory_resource>
#include <string_view>
#include <vector>

namespace Tina::Core {

inline constexpr u64 MaxReadFileBytes = 256ULL * 1024ULL * 1024ULL;

struct ReadFileConfig final {
    u64 maxBytes = MaxReadFileBytes;
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Synchronously reads an entire regular file into owning PMR bytes.
// Path is UTF-8. Empty path, embedded NUL, directories, and size > maxBytes fail
// before large allocation. Does not perform path canonicalization or async IO.
[[nodiscard]] Result<std::pmr::vector<std::byte>> readFile(std::string_view utf8Path, ReadFileConfig config);

} // namespace Tina::Core
