#include <tina/editor/TileMapAuthoringFile.hpp>

#include <tina/core/io/WriteFile.hpp>
#include <tina/editor/TileMapAuthoringDocument.hpp>

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Tina::Editor {
namespace {

struct PendingArtifactWrite final {
    std::string path{};
    const TileMapCookPreviewArtifact* artifact = nullptr;
};

[[nodiscard]] Core::Result<std::string>
joinArtifactPath(std::string_view utf8Root,
                 const AssetFormat::CookedArtifactPath& relative)
{
    if (utf8Root.empty() || utf8Root.find('\0') != std::string_view::npos) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "TileMap save root path is invalid");
    }
    try {
        std::string path{utf8Root};
        if (path.back() != '/' && path.back() != '\\') {
            path.push_back('/');
        }
        path.append(relative.view());
        return path;
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "TileMap save path allocation failed");
    }
}

} // namespace

Core::Result<TileMapAuthoringSaveResult>
saveTileMapAuthoringDocument(std::string_view utf8Root,
                             const TileMapAuthoringDocument& document,
                             AssetFormat::TargetPlatform platform)
{
    auto preview = document.cookPreview(platform);
    if (!preview) {
        return Core::failure(std::move(preview.error()).withContext(
            "saveTileMapAuthoringDocument", "cookPreview"));
    }

    try {
        std::vector<PendingArtifactWrite> writes;
        writes.reserve(preview->artifacts.size());
        const TileMapCookPreviewArtifact* rootArtifact = nullptr;
        Core::u64 byteCount = 0;
        for (const TileMapCookPreviewArtifact& artifact : preview->artifacts) {
            if (artifact.cookedBytes.empty()) {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "TileMap cook preview returned an empty artifact");
            }
            if (artifact.cookedBytes.size() >
                std::numeric_limits<Core::u64>::max() - byteCount) {
                return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                     "TileMap saved byte count overflowed");
            }
            byteCount += static_cast<Core::u64>(artifact.cookedBytes.size());

            if (artifact.assetKind == AssetFormat::AssetKind::TileMap) {
                if (rootArtifact != nullptr) {
                    return Core::failure(Core::CoreErrorCode::Internal,
                                         "TileMap cook preview returned multiple roots");
                }
                rootArtifact = &artifact;
                continue;
            }
            if (artifact.assetKind != AssetFormat::AssetKind::TileMapChunk) {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "TileMap cook preview returned an unexpected artifact kind");
            }
            auto path = joinArtifactPath(utf8Root, artifact.path);
            if (!path) {
                return Core::failure(std::move(path.error()));
            }
            writes.push_back(PendingArtifactWrite{
                .path = std::move(*path),
                .artifact = &artifact,
            });
        }
        if (rootArtifact == nullptr) {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "TileMap cook preview returned no root artifact");
        }
        auto rootPath = joinArtifactPath(utf8Root, rootArtifact->path);
        if (!rootPath) {
            return Core::failure(std::move(rootPath.error()));
        }
        writes.push_back(PendingArtifactWrite{
            .path = std::move(*rootPath),
            .artifact = rootArtifact,
        });

        for (const PendingArtifactWrite& write : writes) {
            auto status = Core::writeFile(
                write.path, write.artifact->cookedBytes,
                Core::WriteFileConfig{.atomicReplace = true, .createParents = true});
            if (!status) {
                return Core::failure(std::move(status.error()).withContext(
                    "saveTileMapAuthoringDocument", "writeCookedArtifact"));
            }
        }
        return TileMapAuthoringSaveResult{
            .artifactCount = writes.size(),
            .byteCount = byteCount,
        };
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "TileMap save plan allocation failed");
    }
}

} // namespace Tina::Editor
