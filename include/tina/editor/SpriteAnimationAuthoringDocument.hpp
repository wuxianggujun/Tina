#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SpriteAnimationClipPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <vector>

namespace Tina::Editor {

namespace SpriteAnimationAuthoringLimits {

inline constexpr Core::usize MaximumHistoryEntries = 256;
inline constexpr Core::usize MaximumHistoryBytes = Core::usize{1} << 30U;

} // namespace SpriteAnimationAuthoringLimits

struct SpriteAnimationAuthoringDocumentConfig final {
    Core::usize frameCapacity = AssetFormat::SpriteAnimationClipWire::MaxFrames;
    // The current state is included, so two entries guarantee one-step undo.
    Core::usize historyEntryCapacity = 32;
    Core::usize historyByteCapacity = 4U * 1024U * 1024U;
};

[[nodiscard]] Core::Status validateSpriteAnimationAuthoringDocumentConfig(
    const SpriteAnimationAuthoringDocumentConfig& config) noexcept;

struct SpriteAnimationAuthoringDesc final {
    Core::AssetId clipId{};
    AssetFormat::SpriteAnimationPlaybackMode playbackMode =
        AssetFormat::SpriteAnimationPlaybackMode::Loop;
    std::vector<AssetFormat::SpriteAnimationFrameDesc> frames{};
};

struct SpriteAnimationCookPreview final {
    Core::u64 documentRevision = 0;
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::WindowsX64;
    Core::AssetId assetId{};
    AssetFormat::CookedArtifactPath path{};
    std::vector<std::byte> cookedBytes{};
};

// Tool-side owner for the unique current SpriteAnimationClip schema. Every
// revision owns canonical payload bytes and the exact sorted Sprite dependency
// stream needed to cook them. Borrowed payloadBytes() views expire after the
// next successful edit, load, undo, redo, or document destruction.
class SpriteAnimationAuthoringDocument final {
public:
    [[nodiscard]] static Core::Result<SpriteAnimationAuthoringDocument>
    Create(const SpriteAnimationAuthoringDesc& initial,
           SpriteAnimationAuthoringDocumentConfig config = {});

    ~SpriteAnimationAuthoringDocument() noexcept = default;

    SpriteAnimationAuthoringDocument(const SpriteAnimationAuthoringDocument&) = delete;
    SpriteAnimationAuthoringDocument& operator=(const SpriteAnimationAuthoringDocument&) = delete;
    SpriteAnimationAuthoringDocument(SpriteAnimationAuthoringDocument&&) noexcept = default;
    SpriteAnimationAuthoringDocument& operator=(SpriteAnimationAuthoringDocument&&) noexcept = default;

    [[nodiscard]] const SpriteAnimationAuthoringDocumentConfig& config() const noexcept
    {
        return m_config;
    }
    [[nodiscard]] Core::u64 revision() const noexcept { return m_revision; }
    [[nodiscard]] Core::AssetId clipId() const noexcept;
    [[nodiscard]] AssetFormat::SpriteAnimationPlaybackMode playbackMode() const noexcept;
    [[nodiscard]] Core::usize frameCount() const noexcept;
    [[nodiscard]] double totalDurationSeconds() const noexcept;
    [[nodiscard]] std::span<const std::byte> payloadBytes() const noexcept;
    [[nodiscard]] std::optional<AssetFormat::SpriteAnimationFrameDesc>
    frameAt(Core::usize index) const noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return m_historyCursor != 0U; }
    [[nodiscard]] bool canRedo() const noexcept { return m_historyCursor + 1U < m_history.size(); }
    [[nodiscard]] Core::usize undoDepth() const noexcept { return m_historyCursor; }
    [[nodiscard]] Core::usize redoDepth() const noexcept
    {
        return m_history.size() - m_historyCursor - 1U;
    }
    [[nodiscard]] Core::usize historyEntryCount() const noexcept { return m_history.size(); }
    [[nodiscard]] Core::usize historyByteCount() const noexcept { return m_historyBytes; }

    [[nodiscard]] Core::Result<SpriteAnimationAuthoringDesc> snapshot() const;

    [[nodiscard]] Core::Status replace(const SpriteAnimationAuthoringDesc& desc);
    [[nodiscard]] Core::Status setClipId(Core::AssetId clipId);
    [[nodiscard]] Core::Status
    setPlaybackMode(AssetFormat::SpriteAnimationPlaybackMode playbackMode);
    [[nodiscard]] Core::Status insertFrame(
        Core::usize index, const AssetFormat::SpriteAnimationFrameDesc& frame);
    [[nodiscard]] Core::Status appendFrame(
        const AssetFormat::SpriteAnimationFrameDesc& frame);
    [[nodiscard]] Core::Status setFrame(
        Core::usize index, const AssetFormat::SpriteAnimationFrameDesc& frame);
    [[nodiscard]] Core::Status setFrameDuration(Core::usize index, float durationSeconds);
    [[nodiscard]] Core::Status duplicateFrame(Core::usize index);
    [[nodiscard]] Core::Status eraseFrame(Core::usize index);
    // destinationIndex is the frame's final index after the move.
    [[nodiscard]] Core::Status moveFrame(Core::usize sourceIndex,
                                         Core::usize destinationIndex);

    // Opens a complete current-schema Cooked asset as a clean baseline.
    [[nodiscard]] Core::Status loadCookedAsset(std::span<const std::byte> cookedBytes);

    [[nodiscard]] Core::Result<SpriteAnimationCookPreview>
    cookPreview(AssetFormat::TargetPlatform platform =
                    AssetFormat::TargetPlatform::WindowsX64) const;

    [[nodiscard]] Core::Status undo() noexcept;
    [[nodiscard]] Core::Status redo() noexcept;

private:
    struct Revision final {
        Core::AssetId clipId{};
        AssetFormat::SpriteAnimationPlaybackMode playbackMode =
            AssetFormat::SpriteAnimationPlaybackMode::Loop;
        std::vector<AssetFormat::SpriteAnimationFrameDesc> frames{};
        std::vector<AssetFormat::CookedAssetWriteDependency> dependencies{};
        std::vector<std::byte> payloadBytes{};
        double totalDurationSeconds = 0.0;
        Core::usize byteCount = 0;
    };

    SpriteAnimationAuthoringDocument(SpriteAnimationAuthoringDocumentConfig config,
                                     std::vector<Revision> history) noexcept;

    [[nodiscard]] const Revision& current() const noexcept { return m_history[m_historyCursor]; }
    [[nodiscard]] Core::Result<Revision>
    buildRevision(const SpriteAnimationAuthoringDesc& desc) const;
    [[nodiscard]] Core::Status commit(Revision candidate);
    [[nodiscard]] Core::Status resetBaseline(Revision candidate);
    void advanceRevision() noexcept;

    SpriteAnimationAuthoringDocumentConfig m_config{};
    std::vector<Revision> m_history{};
    Core::usize m_historyCursor = 0;
    Core::usize m_historyBytes = 0;
    Core::u64 m_revision = 1;
};

} // namespace Tina::Editor
