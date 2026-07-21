#pragma once

#include <tina/core/id/GenerationId.hpp>

namespace Tina::Audio {

namespace Detail {
struct AudioVoiceRegistryTag final {
};
} // namespace Detail

// Runtime-only voice identity. Not an AssetId; AudioEngine validates owner and
// generation before resolving a slot.
using AudioVoiceId = Core::GenerationId<Detail::AudioVoiceRegistryTag>;

} // namespace Tina::Audio
