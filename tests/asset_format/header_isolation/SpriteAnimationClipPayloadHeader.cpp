#include <tina/asset_format/SpriteAnimationClipPayload.hpp>

static_assert(Tina::AssetFormat::SpriteAnimationClipWire::HeaderBytes == 16);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::FrameBytes == 8);
static_assert(static_cast<Tina::Core::u8>(Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong) == 3);
