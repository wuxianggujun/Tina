#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/ContentHash.hpp>

#include <span>

namespace Tina::Core {

enum class ContentHashAlgorithm : u8 {
    Invalid = 0,
    Xxh3_128V1 = 1,
};

// Digests caller-owned bytes into a versioned ContentHash. Public headers do not expose xxHash
// types; algorithm details are fixed by ContentHashAlgorithm and format contracts.
[[nodiscard]] Result<ContentHash> digestContentHash(std::span<const std::byte> bytes,
                                                    ContentHashAlgorithm algorithm);

[[nodiscard]] Result<ContentHash> digestContentHashV1(std::span<const std::byte> bytes);

} // namespace Tina::Core
