#pragma once

#include <tina/asset_format/Fx2DPayload.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <utility>
#include <vector>

namespace Tina::Editor {

struct Fx2DAuthoringDocumentConfig final {
    Core::usize historyEntryCapacity = 32;
    Core::usize historyByteCapacity = 1024U * 1024U;
};

class Fx2DAuthoringDocument final {
public:
    [[nodiscard]] static Core::Result<Fx2DAuthoringDocument> Create(
        const AssetFormat::Fx2DPayloadDesc& initial,
        Fx2DAuthoringDocumentConfig config = {});

    [[nodiscard]] Core::u64 revision() const noexcept { return m_revision; }
    [[nodiscard]] const AssetFormat::Fx2DPayloadDesc& value() const noexcept
    {
        return m_history[m_cursor].value;
    }
    [[nodiscard]] std::span<const std::byte> payloadBytes() const noexcept
    {
        return m_history[m_cursor].bytes;
    }
    [[nodiscard]] bool canUndo() const noexcept { return m_cursor != 0U; }
    [[nodiscard]] bool canRedo() const noexcept { return m_cursor + 1U < m_history.size(); }
    [[nodiscard]] Core::Status replace(const AssetFormat::Fx2DPayloadDesc& value);
    [[nodiscard]] Core::Status undo() noexcept;
    [[nodiscard]] Core::Status redo() noexcept;

private:
    struct Revision final {
        AssetFormat::Fx2DPayloadDesc value{};
        std::vector<std::byte> bytes{};
    };
    Fx2DAuthoringDocument(
        Fx2DAuthoringDocumentConfig config,
        std::vector<Revision> history) noexcept
        : m_config(config), m_history(std::move(history))
    {
    }

    void advanceRevision() noexcept;

    Fx2DAuthoringDocumentConfig m_config{};
    std::vector<Revision> m_history{};
    Core::usize m_cursor = 0;
    Core::usize m_historyBytes = 0;
    Core::u64 m_revision = 1;
};

} // namespace Tina::Editor
