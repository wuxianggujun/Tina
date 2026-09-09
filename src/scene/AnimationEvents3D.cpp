#include <tina/scene/AnimationEvents3D.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::Scene {

Core::Result<AnimationEventBatch3D> collectAnimationEvents3D(
    std::span<const AssetFormat::AnimationEvent3D> events,
    AssetFormat::AnimationClip3DPlaybackMode mode, float durationSeconds,
    double phaseSeconds, double travelSeconds, std::span<AnimationEventCrossing3D> output) noexcept
{
    using Mode = AssetFormat::AnimationClip3DPlaybackMode;
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0 || !std::isfinite(phaseSeconds) ||
        !std::isfinite(travelSeconds) || !std::isfinite(phaseSeconds + travelSeconds) ||
        (mode != Mode::Once && mode != Mode::Loop && mode != Mode::PingPong) ||
        events.size() > AssetFormat::AnimationClip3DWire::MaxEvents) {
        return Core::failure(SceneErrorCode::InvalidAnimation, "Invalid animation event traversal");
    }
    for (const auto& event : events) {
        if (!std::isfinite(event.timeSeconds) || event.timeSeconds < 0 ||
            event.timeSeconds > durationSeconds || event.eventTag == 0) {
            return Core::failure(SceneErrorCode::InvalidAnimation, "Invalid animation event");
        }
    }
    AnimationEventBatch3D result;
    if (travelSeconds == 0.0) { return result; }
    const bool reverse = travelSeconds < 0.0;
    const double start = mode == Mode::Once ? std::clamp(phaseSeconds, 0.0, double(durationSeconds)) : phaseSeconds;
    const double end = mode == Mode::Once
        ? std::clamp(phaseSeconds + travelSeconds, 0.0, double(durationSeconds)) : phaseSeconds + travelSeconds;
    Core::u64 total = 0;
    constexpr auto MaximumCount = (std::numeric_limits<Core::u64>::max)();
    const auto earlier = [](const AnimationEventCrossing3D& left, const AnimationEventCrossing3D& right) {
        if (left.travelSeconds != right.travelSeconds) { return left.travelSeconds < right.travelSeconds; }
        if (left.eventTag != right.eventTag) { return left.eventTag < right.eventTag; }
        return left.clipTimeSeconds < right.clipTimeSeconds;
    };
    const auto insert = [&](AnimationEventCrossing3D crossing) {
        if (output.empty()) { return; }
        Core::usize index = result.written;
        if (index == output.size()) {
            if (!earlier(crossing, output.back())) { return; }
            --index;
        } else { ++result.written; }
        while (index != 0 && earlier(crossing, output[index - 1])) {
            output[index] = output[index - 1];
            --index;
        }
        output[index] = crossing;
    };
    for (const auto& event : events) {
        const auto occurrences = [&](double base, double period, bool backwardLane) {
            double count = 0;
            double first = base;
            if (period == 0) {
                count = reverse ? (base < start && base >= end ? 1.0 : 0.0)
                                : (base > start && base <= end ? 1.0 : 0.0);
            } else {
                const double firstCycle = reverse ? std::ceil((start - base) / period) - 1.0
                                                  : std::floor((start - base) / period) + 1.0;
                const double lastCycle = reverse ? std::ceil((end - base) / period)
                                                 : std::floor((end - base) / period);
                count = (std::max)(0.0, reverse ? firstCycle - lastCycle + 1.0 : lastCycle - firstCycle + 1.0);
                first = base + firstCycle * period;
            }
            const Core::u64 occurrenceCount = count >= static_cast<double>(MaximumCount)
                ? MaximumCount : static_cast<Core::u64>(count);
            total = occurrenceCount > MaximumCount - total ? MaximumCount : total + occurrenceCount;
            const Core::usize emitCount = static_cast<Core::usize>((std::min)(occurrenceCount,
                static_cast<Core::u64>(output.size())));
            for (Core::usize index = 0; index < emitCount; ++index) {
                const double time = first + (reverse ? -1.0 : 1.0) * static_cast<double>(index) * period;
                insert({event.eventTag, event.timeSeconds, std::abs(time - start), backwardLane != reverse});
            }
        };
        if (mode == Mode::PingPong) {
            const double period = 2.0 * durationSeconds;
            occurrences(event.timeSeconds, period, false);
            // Endpoints belong to one lane only, preventing duplicate bounce events.
            if (event.timeSeconds > 0 && event.timeSeconds < durationSeconds) {
                occurrences(period - event.timeSeconds, period, true);
            }
        } else {
            occurrences(event.timeSeconds, mode == Mode::Loop ? double(durationSeconds) : 0.0, false);
        }
    }
    result.dropped = total - static_cast<Core::u64>(result.written);
    return result;
}

} // namespace Tina::Scene
