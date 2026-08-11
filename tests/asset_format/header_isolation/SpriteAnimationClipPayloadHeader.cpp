#include <tina/asset_format/SpriteAnimationClipPayload.hpp>

static_assert(Tina::AssetFormat::SpriteAnimationClipWire::SchemaVersion == 2);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::HeaderBytes == 32);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::FrameBytes == 12);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::EventBytes == 8);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::HeaderBytes % 4 == 0);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::FrameBytes % 4 == 0);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::EventBytes % 4 == 0);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::MaxEventsPerFrame == 64);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::MaxTotalEvents == 16384);
static_assert(Tina::AssetFormat::SpriteAnimationClipWire::EventNameIndexNone == 0xFFFF);
static_assert(static_cast<Tina::Core::u8>(Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong) == 3);
