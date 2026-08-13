#include <tina/editor/Fx2DAuthoringDocument.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <limits>
#include <new>
#include <utility>

namespace Tina::Editor {

Core::Result<Fx2DAuthoringDocument> Fx2DAuthoringDocument::Create(
    const AssetFormat::Fx2DPayloadDesc& initial, Fx2DAuthoringDocumentConfig config)
{
    if (config.historyEntryCapacity == 0U || config.historyByteCapacity == 0U) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Fx2D authoring history capacities must be non-zero");
    }
    auto bytes = AssetFormat::writeFx2DPayloadBytes(initial);
    if (!bytes) {
        return Core::failure(std::move(bytes.error()));
    }
    if (bytes->size() > config.historyByteCapacity) {
        return Core::failure(
            EditorErrorCode::HistoryCapacityExceeded,
            "Fx2D initial revision exceeds history capacity");
    }
    try {
        std::vector<Revision> history;
        history.reserve(config.historyEntryCapacity);
        history.push_back({.value = initial, .bytes = std::move(*bytes)});
        Fx2DAuthoringDocument document{config, std::move(history)};
        document.m_historyBytes = document.m_history.front().bytes.size();
        return document;
    } catch (const std::bad_alloc&) {
        return Core::failure(
            Core::CoreErrorCode::OutOfMemory,
            "Fx2D authoring document allocation failed");
    }
}

Core::Status Fx2DAuthoringDocument::replace(const AssetFormat::Fx2DPayloadDesc& value)
{
    auto bytes = AssetFormat::writeFx2DPayloadBytes(value);
    if (!bytes) {
        return Core::failure(std::move(bytes.error()));
    }
    if (*bytes == m_history[m_cursor].bytes) {
        return Core::success();
    }

    Core::usize retainedBytes = 0;
    for (Core::usize index = 0; index <= m_cursor; ++index) {
        retainedBytes += m_history[index].bytes.size();
    }
    if (m_cursor + 1U >= m_config.historyEntryCapacity ||
        retainedBytes + bytes->size() > m_config.historyByteCapacity) {
        return Core::failure(
            EditorErrorCode::HistoryCapacityExceeded,
            "Fx2D authoring history capacity exceeded");
    }

    try {
        Revision candidate{.value = value, .bytes = std::move(*bytes)};
        m_history.erase(
            m_history.begin() + static_cast<std::ptrdiff_t>(m_cursor + 1U),
            m_history.end());
        m_history.push_back(std::move(candidate));
        m_cursor = m_history.size() - 1U;
        m_historyBytes = retainedBytes + m_history.back().bytes.size();
        advanceRevision();
        return Core::success();
    } catch (const std::bad_alloc&) {
        return Core::failure(
            Core::CoreErrorCode::OutOfMemory,
            "Fx2D authoring commit allocation failed");
    }
}

Core::Status Fx2DAuthoringDocument::undo() noexcept
{
    if (!canUndo()) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Fx2D has no undo revision");
    }
    --m_cursor;
    advanceRevision();
    return Core::success();
}

Core::Status Fx2DAuthoringDocument::redo() noexcept
{
    if (!canRedo()) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Fx2D has no redo revision");
    }
    ++m_cursor;
    advanceRevision();
    return Core::success();
}

void Fx2DAuthoringDocument::advanceRevision() noexcept
{
    if (m_revision != (std::numeric_limits<Core::u64>::max)()) {
        ++m_revision;
    }
}

} // namespace Tina::Editor
