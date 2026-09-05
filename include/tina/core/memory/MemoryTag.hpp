#pragma once

#include <tina/core/base/Types.hpp>

#include <string_view>
#include <utility>

namespace Tina::Core {

// Stable diagnostic categories. Values are append-only so captures from different
// engine revisions remain comparable.
enum class MemoryTag : u8 {
    Invalid = 0,
    Core = 1,
    Platform = 2,
    Task = 3,
    RuntimePersistent = 4,
    RuntimeFrame = 5,
    Scene = 6,
    Asset = 7,
    RenderCpu = 8,
    UI = 9,
    Audio = 10,
    Physics2D = 11,
    Cooker = 12,
    Network = 13,
    Gameplay = 14,
    Animation3D = 15,
    Localization = 16,
    Count = 17,
};

inline constexpr usize MemoryTagCount = std::to_underlying(MemoryTag::Count);

[[nodiscard]] constexpr bool isValidMemoryTag(MemoryTag tag) noexcept
{
    const auto value = std::to_underlying(tag);
    return value > std::to_underlying(MemoryTag::Invalid)
        && value < std::to_underlying(MemoryTag::Count);
}

[[nodiscard]] constexpr usize memoryTagIndex(MemoryTag tag) noexcept
{
    return static_cast<usize>(std::to_underlying(tag));
}

[[nodiscard]] constexpr std::string_view memoryTagName(MemoryTag tag) noexcept
{
    switch (tag) {
    case MemoryTag::Core:
        return "Core";
    case MemoryTag::Platform:
        return "Platform";
    case MemoryTag::Task:
        return "Task";
    case MemoryTag::RuntimePersistent:
        return "RuntimePersistent";
    case MemoryTag::RuntimeFrame:
        return "RuntimeFrame";
    case MemoryTag::Scene:
        return "Scene";
    case MemoryTag::Asset:
        return "Asset";
    case MemoryTag::RenderCpu:
        return "RenderCpu";
    case MemoryTag::UI:
        return "UI";
    case MemoryTag::Audio:
        return "Audio";
    case MemoryTag::Physics2D:
        return "Physics2D";
    case MemoryTag::Cooker:
        return "Cooker";
    case MemoryTag::Network:
        return "Network";
    case MemoryTag::Gameplay:
        return "Gameplay";
    case MemoryTag::Animation3D:
        return "Animation3D";
    case MemoryTag::Localization:
        return "Localization";
    case MemoryTag::Invalid:
    case MemoryTag::Count:
        return "Invalid";
    }
    return "Invalid";
}

} // namespace Tina::Core
