#pragma once

#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/audio/AudioTypes.hpp>
#include <tina/core/error/Result.hpp>

namespace Tina::Audio {

// Map a cooked AudioClip payload view to a non-owning AudioPcmClipView for
// bindVoiceClip / playOneShotPcm. Caller keeps the cooked payload (or lease)
// alive for the duration of playback (docs/audio.md).
[[nodiscard]] inline Core::Result<AudioPcmClipView>
pcmClipViewFromAudioClipPayload(const AssetFormat::AudioClipPayloadView& clip) noexcept
{
    if (clip.empty())
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "AudioClip payload is empty");
    }
    return AudioPcmClipView{
        .frames = clip.interleavedPcm.data(),
        .frameCount = clip.frameCount,
        .channels = clip.channels,
        .sampleRate = clip.sampleRate,
    };
}

} // namespace Tina::Audio
