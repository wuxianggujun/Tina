#include <tina/render/FramePin.hpp>
#include <tina/render/RenderFramePacket.hpp>

namespace {
[[maybe_unused]] constexpr Tina::Render::FramePinKind kKind = Tina::Render::FramePinKind::Surface;
[[maybe_unused]] constexpr Tina::Core::u32 kMaxPins = Tina::Render::RenderFramePacket::MaxPins;
} // namespace
