#include <tina/core/hash/ContentHashDigest.hpp>

#include <xxhash.h>

#include <cstring>

namespace Tina::Core {
namespace {

[[nodiscard]] ContentHash::Bytes encodeXxh3_128LittleEndian(XXH128_hash_t digest) noexcept
{
    ContentHash::Bytes bytes{};
    for (std::size_t index = 0; index < 8U; ++index)
    {
        bytes[index] = static_cast<std::byte>((digest.low64 >> (index * 8U)) & 0xFFU);
        bytes[index + 8U] = static_cast<std::byte>((digest.high64 >> (index * 8U)) & 0xFFU);
    }
    return bytes;
}

} // namespace

Result<ContentHash> digestContentHash(std::span<const std::byte> bytes, ContentHashAlgorithm algorithm)
{
    if (algorithm != ContentHashAlgorithm::Xxh3_128V1)
    {
        return failure(CoreErrorCode::Unsupported, "unsupported content hash algorithm");
    }
    return digestContentHashV1(bytes);
}

Result<ContentHash> digestContentHashV1(std::span<const std::byte> bytes)
{
    // XXH3-128 v1: default seed 0, no secret. Empty input is legal and deterministic.
    const auto digest = XXH3_128bits(bytes.data(), bytes.size());
    const auto encoded = encodeXxh3_128LittleEndian(digest);
    auto contentHash = ContentHash::fromBytes(encoded);
    if (!contentHash)
    {
        return failure(CoreErrorCode::Internal, "content hash digest produced an invalid zero value");
    }
    return *contentHash;
}

} // namespace Tina::Core
