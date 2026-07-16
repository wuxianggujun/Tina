#include <tina/runtime/EngineConfig.hpp>

#include <tina/runtime/RuntimeErrors.hpp>

#include <cmath>
#include <string_view>

namespace Tina {
namespace {

[[nodiscard]] bool isContinuationByte(unsigned char value) noexcept
{
    return (value & 0xC0U) == 0x80U;
}

[[nodiscard]] bool isValidUtf8(std::string_view text) noexcept
{
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7FU) {
            if (first == 0U) {
                return false;
            }
            ++index;
            continue;
        }

        std::size_t continuationCount = 0;
        char32_t codePoint = 0;
        char32_t minimumCodePoint = 0;
        if ((first & 0xE0U) == 0xC0U) {
            continuationCount = 1;
            codePoint = first & 0x1FU;
            minimumCodePoint = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            continuationCount = 2;
            codePoint = first & 0x0FU;
            minimumCodePoint = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            continuationCount = 3;
            codePoint = first & 0x07U;
            minimumCodePoint = 0x10000U;
        } else {
            return false;
        }

        if (continuationCount > text.size() - index - 1) {
            return false;
        }
        for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if (!isContinuationByte(next)) {
                return false;
            }
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }

        if (codePoint < minimumCodePoint || codePoint > 0x10FFFFU
            || (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
            return false;
        }
        index += continuationCount + 1;
    }
    return true;
}

[[nodiscard]] Core::Status invalidConfig(std::string_view message)
{
    return Core::failure(ConfigurationErrorCode::InvalidEngineConfig, message);
}

} // namespace

EngineConfig EngineConfig::Defaults()
{
    return EngineConfig{
        .applicationName = "Tina",
        .fixedSimulation = Core::FixedStepConfig{},
        .gameplayTimeScale = 1.0,
        .shutdownDeadline = Core::Duration{5.0},
    };
}

Core::Status EngineConfig::validate() const
{
    if (applicationName.empty()) {
        return invalidConfig("applicationName must not be empty");
    }
    if (!isValidUtf8(applicationName)) {
        return invalidConfig("applicationName must be valid UTF-8 without embedded NUL bytes");
    }
    if (!std::isfinite(gameplayTimeScale) || gameplayTimeScale < 0.0) {
        return invalidConfig("gameplayTimeScale must be finite and non-negative");
    }
    if (!std::isfinite(shutdownDeadline.count()) || shutdownDeadline.count() <= 0.0) {
        return invalidConfig("shutdownDeadline must be finite and greater than zero");
    }
    if (fixedSimulation.maximumStepsPerFrame > MaximumFixedStepsPerFrame) {
        return invalidConfig("fixedSimulation.maximumStepsPerFrame must not exceed four");
    }
    if (auto accumulator = Core::FixedStepAccumulator::Create(fixedSimulation); !accumulator) {
        auto error = Core::Error{
            ConfigurationErrorCode::InvalidEngineConfig,
            "fixedSimulation is invalid"};
        error.addContext("EngineConfig::validate", accumulator.error().message);
        return Core::failure(std::move(error));
    }
    return Core::success();
}

} // namespace Tina
