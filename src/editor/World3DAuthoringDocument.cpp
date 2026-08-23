#include <tina/editor/World3DAuthoringDocument.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Editor {
namespace {

using AssetFormat::PrefabNodeDesc;
using AssetFormat::PrefabNodeView;
using AssetFormat::PrefabPayloadDesc;

inline constexpr Core::usize MinimumPayloadBytes =
    AssetFormat::PrefabWire::HeaderBytes + AssetFormat::PrefabWire::NodeBytes;

[[nodiscard]] Core::Status allocationFailure()
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "World3D authoring document allocation failed");
}

[[nodiscard]] Core::Status validateStableNodeIds(std::span<const PrefabNodeDesc> nodes) noexcept
{
    for (Core::usize index = 0; index < nodes.size(); ++index)
    {
        if (nodes[index].stableNodeId == 0U)
        {
            return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                 "World3D authoring stable node ID must be non-zero");
        }
        for (Core::usize previous = 0; previous < index; ++previous)
        {
            if (nodes[previous].stableNodeId == nodes[index].stableNodeId)
            {
                return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                     "World3D authoring stable node IDs must be unique");
            }
        }
    }
    return Core::success();
}

[[nodiscard]] PrefabNodeDesc toDesc(const PrefabNodeView& node) noexcept
{
    return PrefabNodeDesc{
        .stableNodeId = node.stableNodeId,
        .parentIndex = node.parentIndex,
        .nodeKind = node.nodeKind,
        .name = node.name,
        .positionX = node.positionX,
        .positionY = node.positionY,
        .positionZ = node.positionZ,
        .rotationX = node.rotationX,
        .rotationY = node.rotationY,
        .rotationZ = node.rotationZ,
        .rotationW = node.rotationW,
        .scaleX = node.scaleX,
        .scaleY = node.scaleY,
        .scaleZ = node.scaleZ,
        .meshId = node.meshId,
        .materialId = node.materialId,
        .visible = node.visible,
        .camera = node.camera,
        .light = node.light,
    };
}

} // namespace

Core::Status validateWorld3DAuthoringDocumentConfig(const World3DAuthoringDocumentConfig& config) noexcept
{
    if (config.nodeCapacity == 0U || config.nodeCapacity > AssetFormat::PrefabWire::MaxNodes)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "World3D authoring node capacity is outside the current schema limit");
    }
    if (config.historyEntryCapacity < 2U ||
        config.historyEntryCapacity > World3DAuthoringLimits::MaximumHistoryEntries)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "World3D authoring history must contain between 2 and 256 entries");
    }
    if (config.historyByteCapacity < MinimumPayloadBytes * 2U ||
        config.historyByteCapacity > World3DAuthoringLimits::MaximumHistoryBytes)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "World3D authoring history byte capacity is outside the supported range");
    }
    return Core::success();
}

Core::Result<World3DAuthoringDocument>
World3DAuthoringDocument::Create(World3DAuthoringDocumentConfig config)
{
    if (const Core::Status status = validateWorld3DAuthoringDocumentConfig(config); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    try
    {
        const std::array rootNodes{PrefabNodeDesc{
            .stableNodeId = World3DAuthoringLimits::DefaultRootStableNodeId,
        }};
        auto rootBytes = AssetFormat::writePrefabPayloadBytes(PrefabPayloadDesc{.nodes = rootNodes});
        if (!rootBytes)
        {
            return Core::failure(std::move(rootBytes.error()));
        }

        std::vector<Revision> history;
        history.reserve(config.historyEntryCapacity);
        history.push_back(Revision{
            .bytes = std::move(*rootBytes),
            .nodeCount = 1,
        });
        return World3DAuthoringDocument{config, std::move(history)};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "World3D authoring history allocation failed");
    }
}

World3DAuthoringDocument::World3DAuthoringDocument(World3DAuthoringDocumentConfig config,
                                                   std::vector<Revision> history) noexcept
    : m_config(config), m_history(std::move(history)), m_historyBytes(m_history.front().bytes.size())
{
}

Core::usize World3DAuthoringDocument::nodeCount() const noexcept
{
    return current().nodeCount;
}

Core::u16 World3DAuthoringDocument::schemaVersion() const noexcept
{
    return current().schemaVersion;
}

std::span<const std::byte> World3DAuthoringDocument::payloadBytes() const noexcept
{
    return current().bytes;
}

Core::Result<AssetFormat::PrefabPayloadView>
World3DAuthoringDocument::parseCurrentPrefab(std::vector<PrefabNodeView>& nodeStorage) const
{
    return AssetFormat::parsePrefabPayload(current().bytes, nodeStorage);
}

Core::Status World3DAuthoringDocument::replace(const PrefabPayloadDesc& desc)
{
    if (desc.nodes.size() > m_config.nodeCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "World3D authoring edit exceeds the configured node capacity");
    }
    if (const Core::Status status = validateStableNodeIds(desc.nodes); !status)
    {
        return status;
    }

    try
    {
        auto bytes = AssetFormat::writePrefabPayloadBytes(desc);
        if (!bytes)
        {
            return Core::failure(std::move(bytes.error()));
        }
        return commit(Revision{
            .bytes = std::move(*bytes),
            .nodeCount = static_cast<Core::u16>(desc.nodes.size()),
        });
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status World3DAuthoringDocument::loadPayload(std::span<const std::byte> payloadBytes)
{
    try
    {
        std::vector<PrefabNodeView> nodeViews;
        auto parsed = AssetFormat::parsePrefabPayload(payloadBytes, nodeViews);
        if (!parsed)
        {
            return Core::failure(std::move(parsed.error()));
        }
        if (parsed->nodes.size() > m_config.nodeCapacity)
        {
            return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                 "World3D authoring payload exceeds the configured node capacity");
        }

        std::vector<PrefabNodeDesc> nodes;
        nodes.reserve(nodeViews.size());
        std::transform(nodeViews.begin(), nodeViews.end(), std::back_inserter(nodes), toDesc);
        if (const Core::Status status = validateStableNodeIds(nodes); !status)
        {
            return status;
        }

        auto canonicalBytes = AssetFormat::writePrefabPayloadBytes(PrefabPayloadDesc{.nodes = nodes});
        if (!canonicalBytes)
        {
            return Core::failure(std::move(canonicalBytes.error()));
        }
        return resetBaseline(Revision{
            .bytes = std::move(*canonicalBytes),
            .schemaVersion = parsed->schemaVersion,
            .nodeCount = static_cast<Core::u16>(nodes.size()),
        });
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status World3DAuthoringDocument::upsertNode(const PrefabNodeDesc& node)
{
    try
    {
        std::vector<PrefabNodeView> nodeViews;
        auto parsed = parseCurrentPrefab(nodeViews);
        if (!parsed)
        {
            return Core::failure(std::move(parsed.error()));
        }

        std::vector<PrefabNodeDesc> nodes;
        nodes.reserve(nodeViews.size() + 1U);
        std::transform(nodeViews.begin(), nodeViews.end(), std::back_inserter(nodes), toDesc);
        const auto existing = std::find_if(nodes.begin(), nodes.end(), [&node](const auto& candidate) {
            return candidate.stableNodeId == node.stableNodeId;
        });
        if (existing == nodes.end())
        {
            if (nodes.size() >= m_config.nodeCapacity)
            {
                return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                     "World3D authoring node capacity is exhausted");
            }
            nodes.push_back(node);
        }
        else
        {
            *existing = node;
        }
        return replace(PrefabPayloadDesc{.nodes = nodes});
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status World3DAuthoringDocument::eraseNodeSubtree(Core::u32 stableNodeId)
{
    try
    {
        std::vector<PrefabNodeView> nodeViews;
        auto parsed = parseCurrentPrefab(nodeViews);
        if (!parsed)
        {
            return Core::failure(std::move(parsed.error()));
        }

        const auto erased = std::find_if(nodeViews.begin(), nodeViews.end(), [stableNodeId](const auto& node) {
            return node.stableNodeId == stableNodeId;
        });
        if (erased == nodeViews.end())
        {
            return Core::failure(EditorErrorCode::EntityNotFound,
                                 "World3D authoring node does not exist");
        }
        const Core::usize erasedIndex = static_cast<Core::usize>(std::distance(nodeViews.begin(), erased));

        std::vector<bool> removed(nodeViews.size(), false);
        for (Core::usize index = 0; index < nodeViews.size(); ++index)
        {
            const Core::i32 parentIndex = nodeViews[index].parentIndex;
            removed[index] = index == erasedIndex ||
                             (parentIndex >= 0 && removed[static_cast<Core::usize>(parentIndex)]);
        }

        std::vector<Core::i32> retainedIndices(nodeViews.size(), -1);
        std::vector<PrefabNodeDesc> retained;
        retained.reserve(nodeViews.size());
        for (Core::usize index = 0; index < nodeViews.size(); ++index)
        {
            if (removed[index])
            {
                continue;
            }
            retainedIndices[index] = static_cast<Core::i32>(retained.size());
            PrefabNodeDesc node = toDesc(nodeViews[index]);
            if (node.parentIndex >= 0)
            {
                node.parentIndex = retainedIndices[static_cast<Core::usize>(node.parentIndex)];
            }
            retained.push_back(node);
        }

        return replace(PrefabPayloadDesc{.nodes = retained});
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status World3DAuthoringDocument::undo() noexcept
{
    if (!canUndo())
    {
        return Core::failure(EditorErrorCode::UndoUnavailable,
                             "World3D authoring document has no undo revision");
    }
    --m_historyCursor;
    advanceRevision();
    return Core::success();
}

Core::Status World3DAuthoringDocument::redo() noexcept
{
    if (!canRedo())
    {
        return Core::failure(EditorErrorCode::RedoUnavailable,
                             "World3D authoring document has no redo revision");
    }
    ++m_historyCursor;
    advanceRevision();
    return Core::success();
}

Core::Status World3DAuthoringDocument::commit(Revision candidate)
{
    if (candidate.bytes == current().bytes)
    {
        return Core::success();
    }
    if (current().bytes.size() + candidate.bytes.size() > m_config.historyByteCapacity)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "World3D authoring history cannot retain an undoable edit");
    }

    const Core::usize retainedEnd = m_historyCursor + 1U;
    for (Core::usize index = retainedEnd; index < m_history.size(); ++index)
    {
        m_historyBytes -= m_history[index].bytes.size();
    }
    m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(retainedEnd), m_history.end());

    // Create() reserves the complete entry budget. Evict before push so a
    // successful edit never expands the history vector implicitly.
    while (m_history.size() > 1U &&
           (m_history.size() >= m_config.historyEntryCapacity ||
            m_historyBytes + candidate.bytes.size() > m_config.historyByteCapacity))
    {
        m_historyBytes -= m_history.front().bytes.size();
        m_history.erase(m_history.begin());
        --m_historyCursor;
    }

    m_historyBytes += candidate.bytes.size();
    m_history.push_back(std::move(candidate));
    m_historyCursor = m_history.size() - 1U;
    advanceRevision();
    return Core::success();
}

Core::Status World3DAuthoringDocument::resetBaseline(Revision candidate)
{
    if (candidate.bytes.size() > m_config.historyByteCapacity)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "World3D authoring baseline exceeds the configured history byte capacity");
    }
    if (m_history.size() == 1U && candidate.bytes == current().bytes)
    {
        return Core::success();
    }

    m_history.clear();
    m_history.push_back(std::move(candidate));
    m_historyCursor = 0;
    m_historyBytes = m_history.front().bytes.size();
    advanceRevision();
    return Core::success();
}

void World3DAuthoringDocument::advanceRevision() noexcept
{
    if (m_revision != (std::numeric_limits<Core::u64>::max)())
    {
        ++m_revision;
    }
}

} // namespace Tina::Editor
