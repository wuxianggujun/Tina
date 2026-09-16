#include "EditorWorkspaceState.hpp"

#include <tina/asset/AssetTypedViews.hpp>
#include <tina/audio/AudioClipView.hpp>
#include <tina/audio/AudioErrors.hpp>
#include <tina/audio/EncodedPcmStreamer.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::resolveAudioPreviewTarget() const noexcept
    -> std::optional<AudioPreviewTarget>
{
    if (assetInspectorActive_) {
        const auto* asset = inspectedProjectAsset();
        if (asset != nullptr &&
            asset->assetKind == Tina::AssetFormat::AssetKind::AudioClip &&
            asset->assetId) {
            return AudioPreviewTarget{.assetId = asset->assetId, .loop = false};
        }
        return std::nullopt;
    }
    if (workspaceMode_ != WorkspaceMode::World2D || !sceneDocumentActive()) {
        return std::nullopt;
    }
    const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
    if (stableId == 0U) {
        return std::nullopt;
    }
    std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document_.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return std::nullopt;
    }
    const auto entity = std::find_if(
        storage.begin(), storage.end(), [stableId](const auto& candidate) {
            return candidate.stableEntityId == stableId;
        });
    if (entity == storage.end() || !entity->resource.has_value()) {
        return std::nullopt;
    }
    auto kind = Tina::Editor::classifyWorld2DNodeTemplate(*entity);
    if (!kind || *kind != Tina::Editor::World2DNodeTemplate::AudioPlayer2D) {
        return std::nullopt;
    }
    if (!entity->resource->assetId) {
        return std::nullopt;
    }
    return AudioPreviewTarget{
        .assetId = entity->resource->assetId,
        .loop = entity->resource->audioLoopMode == 1U,
    };
}

auto EditorWorkspaceState::refreshAudioPreviewUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    const auto target = resolveAudioPreviewTarget();
    const bool visible = target.has_value() && !fxEditingContext() &&
                         !tileMapEditingContext();
    audioPreviewRowLayout_.visibility =
        visible ? UI::UIVisibility::Visible : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(audioPreviewRow_, audioPreviewRowLayout_);
        !status) {
        return status;
    }
    const bool canPlay = visible && authoringEnabled() && !playSessionActive();
    if (auto status = tree.setEnabled(audioPreviewPlayButton_, canPlay); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(audioPreviewStopButton_,
                                      canPlay && audioPreviewPlaying_);
        !status) {
        return status;
    }
    std::string_view statusText = "Select an AudioClip to preview";
    if (visible) {
        if (playSessionActive()) {
            statusText = "Stop Play before previewing audio";
        } else if (audioPreviewPlaying_) {
            statusText = "Playing";
        } else {
#if defined(TINA_EDITOR_AUDIO_MINIAUDIO)
            statusText = audioPreviewDeviceFailed_
                             ? "Audio device unavailable"
                             : "Ready";
#else
            statusText = "Ready (this build has no playback device)";
#endif
        }
    }
    return tree.setText(audioPreviewStatus_, statusText);
}

auto EditorWorkspaceState::stopAudioPreview(Tina::Audio::AudioEngine* audio)
    -> Tina::Core::Status
{
    pendingAudioPreviewPlay_ = false;
    pendingAudioPreviewStop_ = false;
    if (audioPreviewStreamer_.has_value()) {
        (void)audioPreviewStreamer_->cancel();
        audioPreviewStreamer_.reset();
    }
    if (audio == nullptr || !audioPreviewVoice_.hasValue()) {
        audioPreviewVoice_ = {};
        audioPreviewAssetId_ = {};
        audioPreviewPlaying_ = false;
        return Tina::Core::success();
    }
    const auto live = audio->isVoiceLive(audioPreviewVoice_);
    if (live && *live) {
        if (auto status = audio->enqueueStop(audioPreviewVoice_); !status) {
            if (status.error().code != Tina::Audio::AudioErrorCode::StaleVoice &&
                status.error().code != Tina::Audio::AudioErrorCode::InvalidVoice) {
                return status;
            }
        }
    }
    audioPreviewVoice_ = {};
    audioPreviewAssetId_ = {};
    audioPreviewPlaying_ = false;
    return Tina::Core::success();
}

auto EditorWorkspaceState::startAudioPreview(
    Tina::Audio::AudioEngine& audio, AudioPreviewTarget target)
    -> Tina::Core::Status
{
    if (auto status = stopAudioPreview(&audio); !status) {
        return status;
    }
    const Tina::Asset::AssetHandle handle =
        loadedAsset(target.assetId, Tina::AssetFormat::AssetKind::AudioClip);
    if (!handle || !assetResources_.system.has_value()) {
        authoringFeedback_ = "Audio preview cancelled: the AudioClip is not loaded";
        return Tina::Core::success();
    }
    const Tina::Asset::CookedAssetFile* file =
        assetResources_.system->tryGet(handle);
    if (file == nullptr) {
        authoringFeedback_ = "Audio preview cancelled: cooked AudioClip is unavailable";
        return Tina::Core::success();
    }
    auto clip = Tina::Asset::parseAudioClipFromCooked(*file);
    if (!clip) {
        return Tina::Core::failure(std::move(clip.error()));
    }
    const Tina::Audio::AudioPlayDesc play{
        .loopMode = target.loop ? Tina::Audio::AudioLoopMode::Loop
                                : Tina::Audio::AudioLoopMode::Once,
    };
    if (clip->storage == Tina::AssetFormat::AudioClipStorage::EncodedStream) {
        auto streamer = Tina::Audio::EncodedPcmStreamer::Start(
            audio, clip->encoded,
            Tina::Audio::EncodedPcmStreamDesc{
                .play = play,
                .bus = Tina::Audio::AudioBusId::Sfx,
                .sourceFrameCount = clip->frameCount,
            });
        if (!streamer) {
            return Tina::Core::failure(std::move(streamer.error()));
        }
        audioPreviewVoice_ = streamer->voice();
        audioPreviewStreamer_ = std::move(*streamer);
    } else {
        auto pcm = Tina::Audio::pcmClipViewFromAudioClipPayload(*clip);
        if (!pcm) {
            return Tina::Core::failure(std::move(pcm.error()));
        }
        auto voice = audio.playPcm(*pcm, play, Tina::Audio::AudioBusId::Sfx);
        if (!voice) {
            return Tina::Core::failure(std::move(voice.error()));
        }
        audioPreviewVoice_ = *voice;
    }
    audioPreviewAssetId_ = target.assetId;
    audioPreviewPlaying_ = true;
    authoringFeedback_ = target.loop ? "Audio preview looping"
                                     : "Audio preview playing";
    return Tina::Core::success();
}

auto EditorWorkspaceState::tickAudioPreview(Tina::Audio::AudioEngine* audio)
    -> Tina::Core::Status
{
    audioPreviewEngine_ = audio;
    if (audio == nullptr) {
        pendingAudioPreviewPlay_ = false;
        pendingAudioPreviewStop_ = false;
        audioPreviewStreamer_.reset();
        audioPreviewVoice_ = {};
        audioPreviewAssetId_ = {};
        audioPreviewPlaying_ = false;
        return Tina::Core::success();
    }
#if defined(TINA_EDITOR_AUDIO_MINIAUDIO)
    if (!audioPreviewDevice_.has_value() && !audioPreviewDeviceFailed_) {
        auto device = Tina::Audio::MiniaudioDevice::Create({
            .useNullBackend = options_.autoDemo || options_.targetFrameCount != 0U,
            .sampleRate = 48000,
            .channels = 2,
            .periodFrames = 256,
        });
        if (!device) {
            audioPreviewDeviceFailed_ = true;
            authoringFeedback_ = "Audio preview device unavailable: ";
            authoringFeedback_ += device.error().message;
        } else {
            device->attachMixer(audio);
            if (auto started = device->start(); started) {
                audioPreviewDevice_.emplace(std::move(*device));
            } else {
                audioPreviewDeviceFailed_ = true;
                authoringFeedback_ = "Audio preview device failed to start: ";
                authoringFeedback_ += started.error().message;
            }
        }
    }
#endif
    if (pendingAudioPreviewStop_) {
        if (auto status = stopAudioPreview(audio); !status) {
            return status;
        }
        authoringFeedback_ = "Audio preview stopped";
    }
    if (pendingAudioPreviewPlay_) {
        pendingAudioPreviewPlay_ = false;
        const auto target = resolveAudioPreviewTarget();
        if (!target.has_value()) {
            authoringFeedback_ =
                "Audio preview cancelled: select an AudioClip or AudioPlayer2D";
        } else if (auto status = startAudioPreview(*audio, *target); !status) {
            return status;
        }
    }
    if (audioPreviewStreamer_.has_value() && !audioPreviewStreamer_->finished()) {
        if (auto status = audioPreviewStreamer_->pump(); !status) {
            return status;
        }
    }
    if (!audioPreviewVoice_.hasValue()) {
        audioPreviewPlaying_ = false;
        return Tina::Core::success();
    }
    const auto live = audio->isVoiceLive(audioPreviewVoice_);
    if (!live || !*live) {
        audioPreviewStreamer_.reset();
        audioPreviewVoice_ = {};
        audioPreviewAssetId_ = {};
        audioPreviewPlaying_ = false;
        return Tina::Core::success();
    }
    const auto playing = audio->isVoicePlaying(audioPreviewVoice_);
    audioPreviewPlaying_ = playing && *playing;
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
