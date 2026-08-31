#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/save/SaveTypes.hpp>

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::Save::Detail {

inline constexpr Core::u16 SaveWireSchemaVersion = 1;
inline constexpr Core::u16 SaveWireHeaderBytes = 64;
inline constexpr Core::usize SaveWireDigestBytes = 16;

struct ParsedSaveFile final {
    SaveSlotMetadata metadata{};
    Core::usize payloadOffset = 0;
};

[[nodiscard]] Core::Result<std::vector<std::byte>> encodeSaveFile(
    const SaveSlotMetadata& metadata,
    std::span<const std::byte> payload);

[[nodiscard]] Core::Result<ParsedSaveFile> parseSaveFile(
    std::span<const std::byte> bytes,
    SaveSlotId expectedSlot,
    std::string_view expectedGameId,
    Core::u64 maxPayloadBytes);

} // namespace Tina::Save::Detail
