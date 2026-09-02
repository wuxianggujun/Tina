#pragma once

#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace Tina::Editor {

namespace World2DAuthoringLimits {

// The initial revision occupies one entry, so a capacity of one would accept the
// config and then refuse every replace.
inline constexpr Core::usize MinimumHistoryEntries = 2;
inline constexpr Core::usize MaximumHistoryEntries = 256;
inline constexpr Core::usize MaximumHistoryBytes = Core::usize{1} << 30U;

} // namespace World2DAuthoringLimits

struct World2DAuthoringDocumentConfig final {
    Core::usize entityCapacity = AssetFormat::World2DSnapshotWire::MaximumEntities;
    Core::usize gameplayByteCapacity = AssetFormat::World2DSnapshotWire::MaximumGameplayBytes;
    // The current state is included, so two entries guarantee one-step undo.
    Core::usize historyEntryCapacity = 32;
    Core::usize historyByteCapacity = 16U * 1024U * 1024U;
};

[[nodiscard]] Core::Status
validateWorld2DAuthoringDocumentConfig(const World2DAuthoringDocumentConfig& config) noexcept;

// Tool-side owner for the unique current World2D snapshot schema. Every
// published revision has passed AssetFormat validation and has canonical wire
// bytes. Borrowed snapshotBytes() views expire on the next successful edit,
// undo, redo, or document destruction.
class World2DAuthoringDocument final {
public:
    [[nodiscard]] static Core::Result<World2DAuthoringDocument>
    Create(World2DAuthoringDocumentConfig config = {});

    ~World2DAuthoringDocument() noexcept = default;

    World2DAuthoringDocument(const World2DAuthoringDocument&) = delete;
    World2DAuthoringDocument& operator=(const World2DAuthoringDocument&) = delete;
    World2DAuthoringDocument(World2DAuthoringDocument&&) noexcept = default;
    World2DAuthoringDocument& operator=(World2DAuthoringDocument&&) noexcept = default;

    [[nodiscard]] const World2DAuthoringDocumentConfig& config() const noexcept { return m_config; }
    [[nodiscard]] Core::u64 revision() const noexcept { return m_revision; }
    [[nodiscard]] Core::usize entityCount() const noexcept;
    [[nodiscard]] Core::usize gameplayByteCount() const noexcept;
    [[nodiscard]] Core::u32 gameplaySchema() const noexcept;
    [[nodiscard]] Core::u32 gameplayVersion() const noexcept;
    [[nodiscard]] std::span<const std::byte> snapshotBytes() const noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return m_historyCursor != 0U; }
    [[nodiscard]] bool canRedo() const noexcept { return m_historyCursor + 1U < m_history.size(); }
    [[nodiscard]] Core::usize undoDepth() const noexcept { return m_historyCursor; }
    [[nodiscard]] Core::usize redoDepth() const noexcept { return m_history.size() - m_historyCursor - 1U; }
    [[nodiscard]] Core::usize historyEntryCount() const noexcept { return m_history.size(); }
    [[nodiscard]] Core::usize historyByteCount() const noexcept { return m_historyBytes; }

    // Parses the current canonical snapshot. On failure, caller-owned entity
    // storage is unchanged, matching AssetFormat parse semantics.
    [[nodiscard]] Core::Result<AssetFormat::World2DSnapshotView>
    parseCurrentSnapshot(std::vector<AssetFormat::World2DEntityDesc>& entityStorage) const;

    // Replaces the whole document as one undoable revision. This is the batch
    // transaction boundary used by inspectors, gizmos, importers, and tools.
    [[nodiscard]] Core::Status replace(const AssetFormat::World2DSnapshotDesc& desc);
    // Opens canonical bytes as a new saved baseline. Success clears undo/redo;
    // failure preserves the existing document and history.
    [[nodiscard]] Core::Status loadSnapshot(std::span<const std::byte> snapshotBytes);

    // Focused authoring operations are also complete revisions. A failed
    // operation preserves the current state and both history branches.
    [[nodiscard]] Core::Status upsertEntity(const AssetFormat::World2DEntityDesc& entity);
    [[nodiscard]] Core::Status eraseEntitySubtree(Core::u32 stableEntityId);
    [[nodiscard]] Core::Status setGameplay(Core::u32 schema, Core::u32 version,
                                           std::span<const std::byte> bytes);

    [[nodiscard]] Core::Status undo() noexcept;
    [[nodiscard]] Core::Status redo() noexcept;

private:
    struct Revision final {
        std::vector<std::byte> bytes{};
        Core::u32 entityCount = 0;
        Core::u32 gameplaySchema = 0;
        Core::u32 gameplayVersion = 0;
        Core::u32 gameplayByteCount = 0;
    };

    World2DAuthoringDocument(World2DAuthoringDocumentConfig config,
                             std::vector<Revision> history) noexcept;

    [[nodiscard]] const Revision& current() const noexcept { return m_history[m_historyCursor]; }
    [[nodiscard]] Core::Status commit(Revision candidate);
    [[nodiscard]] Core::Status resetBaseline(Revision candidate);
    void advanceRevision() noexcept;

    World2DAuthoringDocumentConfig m_config{};
    std::vector<Revision> m_history{};
    Core::usize m_historyCursor = 0;
    Core::usize m_historyBytes = 0;
    Core::u64 m_revision = 1;
};

} // namespace Tina::Editor
