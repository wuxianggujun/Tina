#pragma once

#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Tina::Asset::GltfDetail {

struct FileSnapshot final {
    std::filesystem::path finalPath{};
    std::uint64_t fileSize = 0;
    std::vector<std::byte> bytes{};
};

// Opens once, validates the opened file's final path, then reads from that same handle.
// requestedBytes == 0 reads the complete file; otherwise the file may contain trailing bytes.
[[nodiscard]] Core::Result<FileSnapshot> readFileSnapshot(
    const std::filesystem::path& requestedPath,
    const std::filesystem::path* containmentRoot,
    std::uint64_t maxFileBytes,
    std::uint64_t requestedBytes = 0) noexcept;

} // namespace Tina::Asset::GltfDetail
