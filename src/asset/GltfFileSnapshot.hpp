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

// GetFinalPathNameByHandleW returns extended-length paths (\\\\?\\C:\\... or
// \\\\?\\UNC\\server\\share\\...). Convert a canonical authoring root to that same
// representation before comparing it with a path obtained from the opened handle.
[[nodiscard]] std::filesystem::path snapshotContainmentPath(std::filesystem::path path);

// Opens once, validates the opened file's final path, then reads from that same handle.
// requestedBytes == 0 reads the complete file; otherwise the file may contain trailing bytes.
// allowShorterFile is probe-only: it returns a stable short snapshot so size drift can be Dirty.
[[nodiscard]] Core::Result<FileSnapshot> readFileSnapshot(
    const std::filesystem::path& requestedPath,
    const std::filesystem::path* containmentRoot,
    std::uint64_t maxFileBytes,
    std::uint64_t requestedBytes = 0,
    bool allowShorterFile = false) noexcept;

} // namespace Tina::Asset::GltfDetail
