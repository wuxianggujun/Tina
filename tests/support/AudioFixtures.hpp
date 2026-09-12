#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Tests {

inline constexpr std::array AudioFixtureNames{
    "tone.wav", "tone.flac", "tone.mp3", "tone-vorbis.ogg", "tone-opus.opus"};

inline std::vector<std::byte> readAudioFixture(std::string_view name)
{
    const auto path = std::filesystem::path{TINA_AUDIO_FIXTURE_DIR} / name;
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file || file.tellg() <= 0 || file.tellg() > 1024 * 1024) {
        throw std::runtime_error("Invalid or missing committed audio fixture: " + std::string{name});
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) { throw std::runtime_error("Could not read audio fixture: " + std::string{name}); }
    return bytes;
}

// Integer and IEEE float RIFF sources, including surround. No codec library in tests.
inline std::vector<std::byte> makePcmWav(std::span<const float> samples,
                                       std::uint16_t channels = 1,
                                       std::uint16_t bits = 16,
                                       bool floatingPoint = false,
                                       std::uint32_t sampleRate = 8000)
{
    std::vector<std::byte> bytes;
    const auto append = [&](std::uint64_t value, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xff));
        }
    };
    const auto tag = [&](std::string_view text) {
        for (const char value : text) { bytes.push_back(static_cast<std::byte>(value)); }
    };
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * (bits / 8));
    tag("RIFF"); append(36 + dataBytes + (dataBytes & 1U), 4); tag("WAVE");
    tag("fmt "); append(16, 4); append(floatingPoint ? 3 : 1, 2); append(channels, 2);
    append(sampleRate, 4); append(sampleRate * channels * (bits / 8), 4);
    append(channels * (bits / 8), 2); append(bits, 2);
    tag("data"); append(dataBytes, 4);
    for (float sample : samples) {
        if (floatingPoint) {
            append(bits == 32 ? std::bit_cast<std::uint32_t>(sample)
                              : std::bit_cast<std::uint64_t>(static_cast<double>(sample)), bits / 8);
        } else if (bits == 8) {
            append(static_cast<std::uint8_t>((sample + 1.0F) * 128.0F), 1);
        } else {
            append(static_cast<std::uint64_t>(static_cast<std::int64_t>(
                static_cast<double>(sample) * static_cast<double>(std::uint64_t{1} << (bits - 1)))), bits / 8);
        }
    }
    if ((dataBytes & 1U) != 0) { append(0, 1); }
    return bytes;
}

} // namespace Tina::Tests
