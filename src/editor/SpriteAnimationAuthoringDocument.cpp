#include <tina/editor/SpriteAnimationAuthoringDocument.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Editor {
namespace {

[[nodiscard]] Core::Status allocationFailure()
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "Sprite animation authoring document allocation failed");
}

[[nodiscard]] Core::Status frameNotFound()
{
    return Core::failure(EditorErrorCode::FrameNotFound,
                         "Sprite animation authoring frame does not exist");
}

[[nodiscard]] bool sameDependency(
    const AssetFormat::CookedAssetWriteDependency& left,
    const AssetFormat::CookedAssetWriteDependency& right) noexcept
{
    return left.assetId == right.assetId && left.expectedKind == right.expectedKind &&
           left.flags == right.flags;
}

} // namespace

Core::Status validateSpriteAnimationAuthoringDocumentConfig(
    const SpriteAnimationAuthoringDocumentConfig& config) noexcept
{
    if (config.frameCapacity == 0U ||
        config.frameCapacity > AssetFormat::SpriteAnimationClipWire::MaxFrames)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Sprite animation frame capacity exceeds the current schema limit");
    }
    if (config.historyEntryCapacity < 2U ||
        config.historyEntryCapacity > SpriteAnimationAuthoringLimits::MaximumHistoryEntries)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Sprite animation history must contain between 2 and 256 entries");
    }
    constexpr Core::usize MinimumRevisionBytes =
        AssetFormat::SpriteAnimationClipWire::HeaderBytes +
        AssetFormat::SpriteAnimationClipWire::FrameBytes +
        sizeof(AssetFormat::SpriteAnimationFrameDesc) +
        sizeof(AssetFormat::CookedAssetWriteDependency);
    if (config.historyByteCapacity < MinimumRevisionBytes * 2U ||
        config.historyByteCapacity > SpriteAnimationAuthoringLimits::MaximumHistoryBytes)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Sprite animation history byte capacity is outside the supported range");
    }
    return Core::success();
}

Core::Result<SpriteAnimationAuthoringDocument>
SpriteAnimationAuthoringDocument::Create(
    const SpriteAnimationAuthoringDesc& initial,
    SpriteAnimationAuthoringDocumentConfig config)
{
    if (const Core::Status status =
            validateSpriteAnimationAuthoringDocumentConfig(config);
        !status)
    {
        return Core::failure(std::move(status.error()));
    }

    try
    {
        std::vector<Revision> history;
        history.reserve(config.historyEntryCapacity);
        SpriteAnimationAuthoringDocument document{config, std::move(history)};
        auto initialRevision = document.buildRevision(initial);
        if (!initialRevision)
        {
            return Core::failure(std::move(initialRevision.error()));
        }
        if (initialRevision->byteCount > config.historyByteCapacity)
        {
            return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                                 "Sprite animation baseline exceeds the configured history byte capacity");
        }
        document.m_history.push_back(std::move(*initialRevision));
        document.m_historyBytes = document.m_history.front().byteCount;
        return document;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Sprite animation authoring history allocation failed");
    }
}

SpriteAnimationAuthoringDocument::SpriteAnimationAuthoringDocument(
    SpriteAnimationAuthoringDocumentConfig config,
    std::vector<Revision> history) noexcept
    : m_config(config), m_history(std::move(history))
{
}

Core::AssetId SpriteAnimationAuthoringDocument::clipId() const noexcept
{
    return current().clipId;
}

AssetFormat::SpriteAnimationPlaybackMode
SpriteAnimationAuthoringDocument::playbackMode() const noexcept
{
    return current().playbackMode;
}

Core::usize SpriteAnimationAuthoringDocument::frameCount() const noexcept
{
    return current().frames.size();
}

double SpriteAnimationAuthoringDocument::totalDurationSeconds() const noexcept
{
    return current().totalDurationSeconds;
}

std::span<const std::byte> SpriteAnimationAuthoringDocument::payloadBytes() const noexcept
{
    return current().payloadBytes;
}

std::optional<AssetFormat::SpriteAnimationFrameDesc>
SpriteAnimationAuthoringDocument::frameAt(Core::usize index) const noexcept
{
    if (index >= current().frames.size())
    {
        return std::nullopt;
    }
    return current().frames[index];
}

Core::Result<SpriteAnimationAuthoringDesc>
SpriteAnimationAuthoringDocument::snapshot() const
{
    try
    {
        SpriteAnimationAuthoringDesc desc{
            .clipId = current().clipId,
            .playbackMode = current().playbackMode,
            .frames = current().frames,
            .frameEvents = current().frameEvents,
        };
        // The copied frames still view the document's storage; repoint them at
        // the snapshot's own event vectors before handing it to the caller.
        desc.rebindFrameEvents();
        return desc;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(allocationFailure().error());
    }
}

Core::Result<SpriteAnimationAuthoringDocument::Revision>
SpriteAnimationAuthoringDocument::buildRevision(
    const SpriteAnimationAuthoringDesc& desc) const
{
    if (!desc.clipId)
    {
        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidIdentity,
                             "Sprite animation authoring requires a clip AssetId");
    }
    if (desc.frames.size() > m_config.frameCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "Sprite animation authoring frame capacity is exhausted");
    }

    try
    {
        const AssetFormat::SpriteAnimationClipPayloadDesc payloadDesc{
            .playbackMode = desc.playbackMode,
            .frames = desc.frames,
        };
        auto dependencies = AssetFormat::makeSpriteAnimationClipDependencies(payloadDesc);
        if (!dependencies)
        {
            return Core::failure(std::move(dependencies.error()));
        }
        if (std::any_of(dependencies->begin(), dependencies->end(),
                        [&desc](const auto& dependency) {
                            return dependency.assetId == desc.clipId;
                        }))
        {
            return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidDependency,
                                 "Sprite animation clip cannot depend on itself");
        }
        auto payload = AssetFormat::writeSpriteAnimationClipPayloadBytes(payloadDesc);
        if (!payload)
        {
            return Core::failure(std::move(payload.error()));
        }

        // Copy the frames, then take ownership of their events and repoint each
        // span at revision-owned storage. Keeping the caller's spans here would
        // leave the revision holding dangling pointers once the caller's desc dies.
        std::vector<AssetFormat::SpriteAnimationFrameDesc> ownedFrames = desc.frames;
        std::vector<std::vector<AssetFormat::SpriteAnimationEventDesc>> ownedFrameEvents;
        ownedFrameEvents.reserve(ownedFrames.size());
        for (const auto& frame : desc.frames)
        {
            ownedFrameEvents.emplace_back(frame.events.begin(), frame.events.end());
        }
        std::vector<AssetFormat::CookedAssetWriteDependency> ownedDependencies =
            std::move(*dependencies);
        double totalDurationSeconds = 0.0;
        for (const auto& frame : desc.frames)
        {
            totalDurationSeconds += static_cast<double>(frame.durationSeconds);
        }
        Core::usize revisionByteCount = payload->capacity();
        const auto addOwnedCapacity = [&revisionByteCount](Core::usize count,
                                                           Core::usize elementBytes) {
            const Core::usize maximum = (std::numeric_limits<Core::usize>::max)();
            if (count > maximum / elementBytes ||
                revisionByteCount > maximum - count * elementBytes)
            {
                return false;
            }
            revisionByteCount += count * elementBytes;
            return true;
        };
        bool eventBytesFit = true;
        for (const auto& events : ownedFrameEvents)
        {
            if (!events.empty() &&
                !addOwnedCapacity(events.capacity(),
                                  sizeof(AssetFormat::SpriteAnimationEventDesc)))
            {
                eventBytesFit = false;
                break;
            }
        }
        if (!eventBytesFit ||
            !addOwnedCapacity(ownedFrames.capacity(), sizeof(ownedFrames.front())) ||
            !addOwnedCapacity(ownedDependencies.capacity(),
                              sizeof(ownedDependencies.front())))
        {
            return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                 "Sprite animation revision byte count overflowed");
        }
        Revision revision{};
        revision.clipId = desc.clipId;
        revision.playbackMode = desc.playbackMode;
        revision.frames = std::move(ownedFrames);
        revision.frameEvents = std::move(ownedFrameEvents);
        revision.dependencies = std::move(ownedDependencies);
        revision.payloadBytes = std::move(*payload);
        revision.totalDurationSeconds = totalDurationSeconds;
        revision.byteCount = revisionByteCount;
        revision.rebindFrameEvents();
        return revision;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(allocationFailure().error());
    }
}

Core::Status SpriteAnimationAuthoringDocument::replace(
    const SpriteAnimationAuthoringDesc& desc)
{
    auto candidate = buildRevision(desc);
    if (!candidate)
    {
        return Core::failure(std::move(candidate.error()));
    }
    return commit(std::move(*candidate));
}

Core::Status SpriteAnimationAuthoringDocument::setClipId(Core::AssetId clipId)
{
    auto desc = snapshot();
    if (!desc)
    {
        return Core::failure(std::move(desc.error()));
    }
    desc->clipId = clipId;
    return replace(*desc);
}

Core::Status SpriteAnimationAuthoringDocument::setPlaybackMode(
    AssetFormat::SpriteAnimationPlaybackMode playbackMode)
{
    auto desc = snapshot();
    if (!desc)
    {
        return Core::failure(std::move(desc.error()));
    }
    desc->playbackMode = playbackMode;
    return replace(*desc);
}

Core::Status SpriteAnimationAuthoringDocument::insertFrame(
    Core::usize index, const AssetFormat::SpriteAnimationFrameDesc& frame)
{
    auto desc = snapshot();
    if (!desc)
    {
        return Core::failure(std::move(desc.error()));
    }
    if (index > desc->frames.size())
    {
        return frameNotFound();
    }
    if (desc->frames.size() >= m_config.frameCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "Sprite animation authoring frame capacity is exhausted");
    }
    try
    {
        // frames and frameEvents are parallel, so the incoming frame's borrowed
        // events must be copied into the matching slot before rebinding spans.
        std::vector<AssetFormat::SpriteAnimationEventDesc> events(
            frame.events.begin(), frame.events.end());
        desc->frames.insert(desc->frames.begin() + static_cast<std::ptrdiff_t>(index), frame);
        desc->frameEvents.resize(desc->frames.size());
        desc->frameEvents.insert(
            desc->frameEvents.begin() + static_cast<std::ptrdiff_t>(index), std::move(events));
        desc->frameEvents.resize(desc->frames.size());
        desc->rebindFrameEvents();
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
    return replace(*desc);
}

Core::Status SpriteAnimationAuthoringDocument::appendFrame(
    const AssetFormat::SpriteAnimationFrameDesc& frame)
{
    return insertFrame(frameCount(), frame);
}

Core::Status SpriteAnimationAuthoringDocument::setFrame(
    Core::usize index, const AssetFormat::SpriteAnimationFrameDesc& frame)
{
    auto desc = snapshot();
    if (!desc)
    {
        return Core::failure(std::move(desc.error()));
    }
    if (index >= desc->frames.size())
    {
        return frameNotFound();
    }
    try
    {
        desc->frames[index] = frame;
        desc->frameEvents.resize(desc->frames.size());
        desc->frameEvents[index].assign(frame.events.begin(), frame.events.end());
        desc->rebindFrameEvents();
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
    return replace(*desc);
}

Core::Status SpriteAnimationAuthoringDocument::setFrameDuration(
    Core::usize index, float durationSeconds)
{
    const auto frame = frameAt(index);
    if (!frame)
    {
        return frameNotFound();
    }
    auto updated = *frame;
    updated.durationSeconds = durationSeconds;
    return setFrame(index, updated);
}

Core::Status SpriteAnimationAuthoringDocument::duplicateFrame(Core::usize index)
{
    const auto frame = frameAt(index);
    if (!frame)
    {
        return frameNotFound();
    }
    return insertFrame(index + 1U, *frame);
}

Core::Status SpriteAnimationAuthoringDocument::eraseFrame(Core::usize index)
{
    auto desc = snapshot();
    if (!desc)
    {
        return Core::failure(std::move(desc.error()));
    }
    if (index >= desc->frames.size())
    {
        return frameNotFound();
    }
    desc->frames.erase(desc->frames.begin() + static_cast<std::ptrdiff_t>(index));
    desc->frameEvents.resize(desc->frames.size() + 1U);
    desc->frameEvents.erase(desc->frameEvents.begin() + static_cast<std::ptrdiff_t>(index));
    desc->rebindFrameEvents();
    return replace(*desc);
}

Core::Status SpriteAnimationAuthoringDocument::moveFrame(
    Core::usize sourceIndex, Core::usize destinationIndex)
{
    auto desc = snapshot();
    if (!desc)
    {
        return Core::failure(std::move(desc.error()));
    }
    if (sourceIndex >= desc->frames.size() || destinationIndex >= desc->frames.size())
    {
        return frameNotFound();
    }
    if (sourceIndex == destinationIndex)
    {
        return Core::success();
    }
    const auto frame = desc->frames[sourceIndex];
    try
    {
        // The frame's events travel with it, so move the parallel slot too.
        desc->frameEvents.resize(desc->frames.size());
        auto events = std::move(desc->frameEvents[sourceIndex]);
        desc->frames.erase(desc->frames.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
        desc->frameEvents.erase(
            desc->frameEvents.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
        desc->frames.insert(
            desc->frames.begin() + static_cast<std::ptrdiff_t>(destinationIndex), frame);
        desc->frameEvents.insert(
            desc->frameEvents.begin() + static_cast<std::ptrdiff_t>(destinationIndex),
            std::move(events));
        desc->rebindFrameEvents();
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
    return replace(*desc);
}

Core::Status SpriteAnimationAuthoringDocument::loadCookedAsset(
    std::span<const std::byte> cookedBytes)
{
    auto asset = AssetFormat::parseCookedAssetView(cookedBytes);
    if (!asset)
    {
        return Core::failure(std::move(asset.error()));
    }
    if (auto status = AssetFormat::verifyCookedAssetContentHash(*asset); !status)
    {
        return status;
    }
    if (asset->header().assetKind != AssetFormat::AssetKind::SpriteAnimationClip ||
        asset->header().assetTypeVersion != AssetFormat::SpriteAnimationClipWire::SchemaVersion)
    {
        return Core::failure(AssetFormat::AssetFormatErrorCode::UnsupportedSchema,
                             "Sprite animation authoring accepts only the current clip schema");
    }

    auto payload = AssetFormat::parseSpriteAnimationClipPayload(asset->payload());
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    if (asset->header().dependencyCount != payload->spriteDependencyCount)
    {
        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidDependency,
                             "Sprite animation Cooked dependency count does not match the payload");
    }

    try
    {
        std::vector<AssetFormat::AssetDependency> dependencies;
        dependencies.reserve(payload->spriteDependencyCount);
        std::vector<bool> referenced(payload->spriteDependencyCount, false);
        for (Core::u32 index = 0; index < payload->spriteDependencyCount; ++index)
        {
            const auto dependency = asset->dependency(index);
            if (!dependency || !dependency->assetId ||
                dependency->expectedKind != AssetFormat::AssetKind::Sprite ||
                dependency->flags != AssetFormat::DependencyFlags::Required ||
                dependency->assetId == asset->header().assetId)
            {
                return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidDependency,
                                     "Sprite animation dependencies must be distinct required Sprite assets");
            }
            dependencies.push_back(*dependency);
        }

        SpriteAnimationAuthoringDesc desc{
            .clipId = asset->header().assetId,
            .playbackMode = payload->playbackMode,
        };
        desc.frames.reserve(payload->frameCount);
        for (Core::u32 index = 0; index < payload->frameCount; ++index)
        {
            const auto frame = payload->frame(index);
            if (!frame || frame->spriteDependencyIndex >= dependencies.size())
            {
                return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidDependency,
                                     "Sprite animation frame dependency index is invalid");
            }
            referenced[frame->spriteDependencyIndex] = true;
            desc.frames.push_back(AssetFormat::SpriteAnimationFrameDesc{
                .spriteId = dependencies[frame->spriteDependencyIndex].assetId,
                .durationSeconds = frame->durationSeconds,
            });

            // Decode this frame's notify events into desc-owned storage. The
            // spans are bound once, after every frame has been appended.
            std::vector<AssetFormat::SpriteAnimationEventDesc> events;
            events.reserve(frame->eventCount);
            for (Core::u32 offset = 0; offset < frame->eventCount; ++offset)
            {
                const auto event = payload->event(frame->eventStartIndex + offset);
                if (!event)
                {
                    return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                         "Sprite animation frame event index is out of range");
                }
                events.push_back(AssetFormat::SpriteAnimationEventDesc{
                    .eventTag = event->eventTag,
                    .normalizedOffset = event->normalizedOffset,
                });
            }
            desc.frameEvents.push_back(std::move(events));
        }
        desc.rebindFrameEvents();
        if (std::find(referenced.begin(), referenced.end(), false) != referenced.end())
        {
            return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidDependency,
                                 "Sprite animation Cooked asset contains an unused dependency");
        }
        auto candidate = buildRevision(desc);
        if (!candidate)
        {
            return Core::failure(std::move(candidate.error()));
        }
        return resetBaseline(std::move(*candidate));
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Result<SpriteAnimationCookPreview>
SpriteAnimationAuthoringDocument::cookPreview(
    AssetFormat::TargetPlatform platform) const
{
    auto cooked = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = AssetFormat::AssetKind::SpriteAnimationClip,
        .assetTypeVersion = AssetFormat::SpriteAnimationClipWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = current().clipId,
        .dependencies = current().dependencies,
        .payload = current().payloadBytes,
        .payloadAlignment = 4,
        .computeContentHash = true,
    });
    if (!cooked)
    {
        return Core::failure(std::move(cooked.error()));
    }
    auto path = AssetFormat::makeCookedArtifactPath(
        AssetFormat::AssetKind::SpriteAnimationClip, current().clipId);
    if (!path)
    {
        return Core::failure(std::move(path.error()));
    }
    return SpriteAnimationCookPreview{
        .documentRevision = m_revision,
        .targetPlatform = platform,
        .assetId = current().clipId,
        .path = *path,
        .cookedBytes = std::move(*cooked),
    };
}

Core::Status SpriteAnimationAuthoringDocument::undo() noexcept
{
    if (!canUndo())
    {
        return Core::failure(EditorErrorCode::UndoUnavailable,
                             "Sprite animation authoring document has no undo revision");
    }
    --m_historyCursor;
    advanceRevision();
    return Core::success();
}

Core::Status SpriteAnimationAuthoringDocument::redo() noexcept
{
    if (!canRedo())
    {
        return Core::failure(EditorErrorCode::RedoUnavailable,
                             "Sprite animation authoring document has no redo revision");
    }
    ++m_historyCursor;
    advanceRevision();
    return Core::success();
}

Core::Status SpriteAnimationAuthoringDocument::commit(Revision candidate)
{
    const auto sameDependencies = [&candidate, this]() noexcept {
        if (candidate.dependencies.size() != current().dependencies.size())
        {
            return false;
        }
        for (Core::usize index = 0; index < candidate.dependencies.size(); ++index)
        {
            if (!sameDependency(candidate.dependencies[index], current().dependencies[index]))
            {
                return false;
            }
        }
        return true;
    };
    if (candidate.clipId == current().clipId &&
        candidate.payloadBytes == current().payloadBytes && sameDependencies())
    {
        return Core::success();
    }
    if (candidate.byteCount > m_config.historyByteCapacity ||
        current().byteCount > m_config.historyByteCapacity - candidate.byteCount)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "Sprite animation history cannot retain an undoable edit");
    }

    const Core::usize retainedEnd = m_historyCursor + 1U;
    for (Core::usize index = retainedEnd; index < m_history.size(); ++index)
    {
        m_historyBytes -= m_history[index].byteCount;
    }
    m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(retainedEnd),
                    m_history.end());

    while (m_history.size() > 1U &&
           (m_history.size() >= m_config.historyEntryCapacity ||
            candidate.byteCount > m_config.historyByteCapacity - m_historyBytes))
    {
        m_historyBytes -= m_history.front().byteCount;
        m_history.erase(m_history.begin());
        --m_historyCursor;
    }
    if (candidate.byteCount > m_config.historyByteCapacity - m_historyBytes)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "Sprite animation history byte capacity is exhausted");
    }

    m_historyBytes += candidate.byteCount;
    m_history.push_back(std::move(candidate));
    m_historyCursor = m_history.size() - 1U;
    advanceRevision();
    return Core::success();
}

Core::Status SpriteAnimationAuthoringDocument::resetBaseline(Revision candidate)
{
    if (candidate.byteCount > m_config.historyByteCapacity)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "Sprite animation baseline exceeds the configured history byte capacity");
    }

    bool same = m_history.size() == 1U && candidate.clipId == current().clipId &&
                candidate.payloadBytes == current().payloadBytes &&
                candidate.dependencies.size() == current().dependencies.size();
    if (same)
    {
        for (Core::usize index = 0; index < candidate.dependencies.size(); ++index)
        {
            if (!sameDependency(candidate.dependencies[index], current().dependencies[index]))
            {
                same = false;
                break;
            }
        }
    }
    if (same)
    {
        return Core::success();
    }

    m_history.clear();
    m_history.push_back(std::move(candidate));
    m_historyCursor = 0;
    m_historyBytes = m_history.front().byteCount;
    advanceRevision();
    return Core::success();
}

void SpriteAnimationAuthoringDocument::advanceRevision() noexcept
{
    if (m_revision != (std::numeric_limits<Core::u64>::max)())
    {
        ++m_revision;
    }
}

} // namespace Tina::Editor
