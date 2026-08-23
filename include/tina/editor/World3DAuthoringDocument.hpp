#pragma once

#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace Tina::Editor {

namespace World3DAuthoringLimits {

inline constexpr Core::u32 DefaultRootStableNodeId = 1;
inline constexpr Core::usize MaximumHistoryEntries = 256;
inline constexpr Core::usize MaximumHistoryBytes = Core::usize{1} << 30U;

} // namespace World3DAuthoringLimits

struct World3DAuthoringDocumentConfig final {
    Core::usize nodeCapacity = AssetFormat::PrefabWire::MaxNodes;
    // The current state is included, so two entries guarantee one-step undo.
    Core::usize historyEntryCapacity = 32;
    Core::usize historyByteCapacity = 16U * 1024U * 1024U;
};

[[nodiscard]] Core::Status
validateWorld3DAuthoringDocumentConfig(const World3DAuthoringDocumentConfig& config) noexcept;

// Tool-side owner for the unique current Prefab payload schema. Every
// published revision is a canonical PrefabPayload v2 with non-zero, unique
// stable node IDs. Create() starts with one transform-only root because the
// current Prefab schema does not represent an empty hierarchy.
//
// Borrowed payloadBytes() views expire on the next successful edit, undo,
// redo, baseline load, or document destruction.
class World3DAuthoringDocument final {
public:
    [[nodiscard]] static Core::Result<World3DAuthoringDocument>
    Create(World3DAuthoringDocumentConfig config = {});

    ~World3DAuthoringDocument() noexcept = default;

    World3DAuthoringDocument(const World3DAuthoringDocument&) = delete;
    World3DAuthoringDocument& operator=(const World3DAuthoringDocument&) = delete;
    World3DAuthoringDocument(World3DAuthoringDocument&&) noexcept = default;
    World3DAuthoringDocument& operator=(World3DAuthoringDocument&&) noexcept = default;

    [[nodiscard]] const World3DAuthoringDocumentConfig& config() const noexcept { return m_config; }
    [[nodiscard]] Core::u64 revision() const noexcept { return m_revision; }
    [[nodiscard]] Core::usize nodeCount() const noexcept;
    [[nodiscard]] Core::u16 schemaVersion() const noexcept;
    [[nodiscard]] std::span<const std::byte> payloadBytes() const noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return m_historyCursor != 0U; }
    [[nodiscard]] bool canRedo() const noexcept { return m_historyCursor + 1U < m_history.size(); }
    [[nodiscard]] Core::usize undoDepth() const noexcept { return m_historyCursor; }
    [[nodiscard]] Core::usize redoDepth() const noexcept { return m_history.size() - m_historyCursor - 1U; }
    [[nodiscard]] Core::usize historyEntryCount() const noexcept { return m_history.size(); }
    [[nodiscard]] Core::usize historyByteCount() const noexcept { return m_historyBytes; }

    // Parses the current canonical payload. Node views borrow from
    // caller-owned nodeStorage until that storage is changed or destroyed.
    [[nodiscard]] Core::Result<AssetFormat::PrefabPayloadView>
    parseCurrentPrefab(std::vector<AssetFormat::PrefabNodeView>& nodeStorage) const;

    // Replaces the complete hierarchy as one undoable revision. This is the
    // transaction boundary for hierarchy reordering and batch tool edits.
    [[nodiscard]] Core::Status replace(const AssetFormat::PrefabPayloadDesc& desc);
    // Opens current-schema canonical payload bytes as a new baseline. Success
    // clears undo/redo; failure preserves the document and both branches.
    [[nodiscard]] Core::Status loadPayload(std::span<const std::byte> payloadBytes);

    // Upsert preserves an existing node's vector position; parentIndex must
    // therefore remain -1 or reference an earlier node. New nodes are appended.
    [[nodiscard]] Core::Status upsertNode(const AssetFormat::PrefabNodeDesc& node);
    // Erases the node and all descendants, then canonicalizes retained parent
    // indices. Prefab v4 cannot erase the final remaining node.
    [[nodiscard]] Core::Status eraseNodeSubtree(Core::u32 stableNodeId);

    [[nodiscard]] Core::Status undo() noexcept;
    [[nodiscard]] Core::Status redo() noexcept;

private:
    struct Revision final {
        std::vector<std::byte> bytes{};
        Core::u16 schemaVersion = AssetFormat::PrefabWire::SchemaVersion;
        Core::u16 nodeCount = 0;
    };

    World3DAuthoringDocument(World3DAuthoringDocumentConfig config,
                             std::vector<Revision> history) noexcept;

    [[nodiscard]] const Revision& current() const noexcept { return m_history[m_historyCursor]; }
    [[nodiscard]] Core::Status commit(Revision candidate);
    [[nodiscard]] Core::Status resetBaseline(Revision candidate);
    void advanceRevision() noexcept;

    World3DAuthoringDocumentConfig m_config{};
    std::vector<Revision> m_history{};
    Core::usize m_historyCursor = 0;
    Core::usize m_historyBytes = 0;
    Core::u64 m_revision = 1;
};

} // namespace Tina::Editor
