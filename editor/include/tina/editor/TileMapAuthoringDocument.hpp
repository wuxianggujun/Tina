#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Tina::Editor {

namespace TileMapAuthoringLimits {

// The initial revision occupies one entry, so a capacity of one would accept the
// config and then refuse every replace.
inline constexpr Core::usize MinimumHistoryEntries = 2;
inline constexpr Core::usize MaximumHistoryEntries = 256;
inline constexpr Core::usize MaximumHistoryBytes = Core::usize{1} << 30U;

} // namespace TileMapAuthoringLimits

struct TileMapAuthoringDocumentConfig final {
    Core::usize layerCapacity = AssetFormat::TileMapWire::MaxLayers;
    Core::usize objectCapacity = AssetFormat::TileMapWire::MaxObjectsPerMap;
    Core::usize chunkCapacity = AssetFormat::TileMapWire::MaxChunkRefsPerMap;
    // The current state is included, so two entries guarantee one-step undo.
    Core::usize historyEntryCapacity = 32;
    Core::usize historyByteCapacity = 16U * 1024U * 1024U;
};

[[nodiscard]] Core::Status
validateTileMapAuthoringDocumentConfig(const TileMapAuthoringDocumentConfig& config) noexcept;

struct TileMapAuthoringProperty final {
    std::string key{};
    std::string value{};

    friend bool operator==(const TileMapAuthoringProperty&,
                           const TileMapAuthoringProperty&) = default;
};

struct TileMapAuthoringObject final {
    AssetFormat::TileMapObjectId stableObjectId = 0;
    AssetFormat::TileMapObjectKind kind = AssetFormat::TileMapObjectKind::Point;
    bool visible = true;
    std::string name{};
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    std::vector<TileMapAuthoringProperty> properties{};

    friend bool operator==(const TileMapAuthoringObject&,
                           const TileMapAuthoringObject&) = default;
};

struct TileMapAuthoringChunk final {
    Core::u32 chunkX = 0;
    Core::u32 chunkY = 0;
    // Row-major and exactly the current edge-aware chunk extent.
    std::vector<Core::u16> cells{};

    friend bool operator==(const TileMapAuthoringChunk&,
                           const TileMapAuthoringChunk&) = default;
};

struct TileMapAuthoringLayer final {
    AssetFormat::TileMapLayerId stableLayerId = 0;
    AssetFormat::TileMapLayerKind kind = AssetFormat::TileMapLayerKind::Tile;
    bool visible = true;
    std::string name{};
    std::vector<TileMapAuthoringProperty> properties{};
    // Tile layers own chunks; object layers own objects. The other collection must be empty.
    std::vector<TileMapAuthoringChunk> chunks{};
    std::vector<TileMapAuthoringObject> objects{};

    friend bool operator==(const TileMapAuthoringLayer&,
                           const TileMapAuthoringLayer&) = default;
};

struct TileMapAuthoringDesc final {
    Core::AssetId tileMapId{};
    Core::AssetId tilesetId{};
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float cellSizeMeters = 1.0F;
    Core::u16 chunkSizeCells = 16;
    std::vector<TileMapAuthoringLayer> layers{};

    friend bool operator==(const TileMapAuthoringDesc&,
                           const TileMapAuthoringDesc&) = default;
};

struct TileMapAuthoringCellEdit final {
    Core::u32 x = 0;
    Core::u32 y = 0;
    Core::u16 localTileId = AssetFormat::TileMapWire::EmptyTileId;
};

struct TileMapAuthoringChunkSource final {
    Core::AssetId assetId{};
    std::span<const std::byte> payloadBytes{};
};

struct TileMapAuthoringChunkPayloadView final {
    Core::AssetId assetId{};
    AssetFormat::TileMapLayerId layerId = 0;
    Core::u32 chunkX = 0;
    Core::u32 chunkY = 0;
    Core::u16 widthCells = 0;
    Core::u16 heightCells = 0;
    Core::u32 nonEmptyCount = 0;
    std::span<const std::byte> payloadBytes{};
};

struct TileMapCookPreviewArtifact final {
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::AssetId assetId{};
    AssetFormat::CookedArtifactPath path{};
    std::vector<std::byte> cookedBytes{};
};

struct TileMapCookPreview final {
    Core::u64 documentRevision = 0;
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::WindowsX64;
    std::vector<TileMapCookPreviewArtifact> artifacts{};
};

// Tool-side owner for the current TileMap root/chunk schema family. Each revision
// atomically owns map and tileset identities, canonical root bytes, and canonical
// non-empty chunk payloads. Borrowed byte views expire after the next successful
// edit, load, undo, redo, or document destruction.
class TileMapAuthoringDocument final {
public:
    [[nodiscard]] static Core::Result<TileMapAuthoringDocument>
    Create(const TileMapAuthoringDesc& initial,
           TileMapAuthoringDocumentConfig config = {});

    ~TileMapAuthoringDocument() noexcept = default;

    TileMapAuthoringDocument(const TileMapAuthoringDocument&) = delete;
    TileMapAuthoringDocument& operator=(const TileMapAuthoringDocument&) = delete;
    TileMapAuthoringDocument(TileMapAuthoringDocument&&) noexcept = default;
    TileMapAuthoringDocument& operator=(TileMapAuthoringDocument&&) noexcept = default;

    [[nodiscard]] const TileMapAuthoringDocumentConfig& config() const noexcept { return m_config; }
    [[nodiscard]] Core::u64 revision() const noexcept { return m_revision; }
    [[nodiscard]] Core::AssetId tileMapId() const noexcept;
    [[nodiscard]] Core::AssetId tilesetId() const noexcept;
    [[nodiscard]] Core::usize layerCount() const noexcept;
    [[nodiscard]] Core::usize chunkCount() const noexcept;
    [[nodiscard]] Core::usize nonEmptyCellCount() const noexcept;
    [[nodiscard]] std::span<const std::byte> rootPayloadBytes() const noexcept;
    [[nodiscard]] std::optional<TileMapAuthoringChunkPayloadView>
    chunkPayloadAt(Core::usize index) const noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return m_historyCursor != 0U; }
    [[nodiscard]] bool canRedo() const noexcept { return m_historyCursor + 1U < m_history.size(); }
    [[nodiscard]] Core::usize undoDepth() const noexcept { return m_historyCursor; }
    [[nodiscard]] Core::usize redoDepth() const noexcept { return m_history.size() - m_historyCursor - 1U; }
    [[nodiscard]] Core::usize historyEntryCount() const noexcept { return m_history.size(); }
    [[nodiscard]] Core::usize historyByteCount() const noexcept { return m_historyBytes; }

    [[nodiscard]] Core::Result<TileMapAuthoringDesc> snapshot() const;

    // Whole-document and focused operations are transactional canonical revisions.
    [[nodiscard]] Core::Status replace(const TileMapAuthoringDesc& desc);
    [[nodiscard]] Core::Status setCells(AssetFormat::TileMapLayerId layerId,
                                        std::span<const TileMapAuthoringCellEdit> edits);
    [[nodiscard]] Core::Status paintCell(AssetFormat::TileMapLayerId layerId,
                                         Core::u32 x, Core::u32 y,
                                         Core::u16 localTileId);
    [[nodiscard]] Core::Status setLayerVisibility(AssetFormat::TileMapLayerId layerId,
                                                  bool visible);
    [[nodiscard]] Core::Status renameLayer(AssetFormat::TileMapLayerId layerId,
                                           std::string name);
    [[nodiscard]] Core::Status addTileLayer(AssetFormat::TileMapLayerId layerId,
                                            std::string name);
    [[nodiscard]] Core::Status addObjectLayer(AssetFormat::TileMapLayerId layerId,
                                              std::string name);
    [[nodiscard]] Core::Status eraseLayer(AssetFormat::TileMapLayerId layerId);
    [[nodiscard]] Core::Status upsertObject(AssetFormat::TileMapLayerId layerId,
                                            TileMapAuthoringObject object);
    [[nodiscard]] Core::Status eraseObject(AssetFormat::TileMapLayerId layerId,
                                           AssetFormat::TileMapObjectId objectId);

    // Opens a complete current-schema payload family as a clean baseline.
    [[nodiscard]] Core::Status loadPayloadFamily(
        Core::AssetId tileMapId, Core::AssetId tilesetId,
        std::span<const std::byte> rootPayload,
        std::span<const TileMapAuthoringChunkSource> chunks);

    [[nodiscard]] Core::Result<TileMapCookPreview>
    cookPreview(AssetFormat::TargetPlatform platform =
                    AssetFormat::TargetPlatform::WindowsX64) const;

    [[nodiscard]] Core::Status undo() noexcept;
    [[nodiscard]] Core::Status redo() noexcept;

private:
    struct ChunkRevision final {
        Core::AssetId assetId{};
        AssetFormat::TileMapLayerId layerId = 0;
        Core::u32 chunkX = 0;
        Core::u32 chunkY = 0;
        Core::u16 widthCells = 0;
        Core::u16 heightCells = 0;
        Core::u32 nonEmptyCount = 0;
        std::vector<std::byte> bytes{};
    };

    struct Revision final {
        Core::AssetId tileMapId{};
        Core::AssetId tilesetId{};
        std::vector<std::byte> rootBytes{};
        std::vector<ChunkRevision> chunks{};
        Core::u32 layerCount = 0;
        Core::u32 nonEmptyCellCount = 0;
        Core::usize byteCount = 0;
    };

    TileMapAuthoringDocument(TileMapAuthoringDocumentConfig config,
                             std::vector<Revision> history) noexcept;

    [[nodiscard]] const Revision& current() const noexcept { return m_history[m_historyCursor]; }
    [[nodiscard]] Core::Result<Revision> buildRevision(const TileMapAuthoringDesc& desc) const;
    [[nodiscard]] Core::Result<TileMapAuthoringDesc> decode(const Revision& revision) const;
    [[nodiscard]] Core::Status commit(Revision candidate);
    [[nodiscard]] Core::Status resetBaseline(Revision candidate);
    void advanceRevision() noexcept;

    TileMapAuthoringDocumentConfig m_config{};
    std::vector<Revision> m_history{};
    Core::usize m_historyCursor = 0;
    Core::usize m_historyBytes = 0;
    Core::u64 m_revision = 1;
};

} // namespace Tina::Editor
