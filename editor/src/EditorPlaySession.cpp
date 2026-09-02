#include <tina/editor/EditorPlaySession.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Editor {
namespace {

[[nodiscard]] bool validConfig(const EditorPlaySessionConfig& config) noexcept
{
    return config.canonicalByteCapacity != 0 &&
           config.canonicalByteCapacity <= std::vector<std::byte>{}.max_size() &&
           std::isfinite(config.fixedStepSeconds) &&
           config.fixedStepSeconds > 0.0 &&
           std::isfinite(config.maximumFrameDeltaSeconds) &&
           config.maximumFrameDeltaSeconds >= config.fixedStepSeconds &&
           config.maximumStepsPerFrame != 0 &&
           std::isfinite(config.fixedStepSeconds *
                         static_cast<double>(config.maximumStepsPerFrame));
}

[[nodiscard]] bool validWorkspace(EditorPlayWorkspace workspace) noexcept
{
    return workspace == EditorPlayWorkspace::TwoD ||
           workspace == EditorPlayWorkspace::ThreeD;
}

} // namespace

Core::Result<EditorPlaySession>
EditorPlaySession::Create(EditorPlaySessionConfig config)
{
    if (!validConfig(config)) {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor play session configuration is invalid");
    }
    return EditorPlaySession{config};
}

EditorPlaySession::EditorPlaySession(EditorPlaySessionConfig config) noexcept
    : m_config(config)
{
}

Core::Status EditorPlaySession::start(
    EditorPlayWorkspace workspace,
    Core::u64 sourceDocumentRevision,
    std::span<const std::byte> canonicalBytes)
{
    if (active()) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor play session is already active");
    }
    if (!validWorkspace(workspace)) {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor play session workspace is invalid");
    }
    if (sourceDocumentRevision == 0 || canonicalBytes.empty()) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor play session requires a canonical document snapshot");
    }
    if (canonicalBytes.size() > m_config.canonicalByteCapacity) {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "Editor play session snapshot exceeds fixed capacity");
    }

    std::vector<std::byte> stagedCanonicalBytes;
    try {
        stagedCanonicalBytes.assign(canonicalBytes.begin(), canonicalBytes.end());
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor play session snapshot allocation failed");
    }

    m_canonicalBytes.swap(stagedCanonicalBytes);
    m_snapshot.workspace = workspace;
    m_snapshot.state = EditorPlayState::Playing;
    m_snapshot.sourceDocumentRevision = sourceDocumentRevision;
    m_snapshot.simulationTickCount = 0;
    m_snapshot.simulatedSeconds = 0.0;
    m_snapshot.accumulatorSeconds = 0.0;
    m_snapshot.stepPending = false;
    advanceRevision();
    return Core::success();
}

Core::Status EditorPlaySession::pause() noexcept
{
    if (m_snapshot.state != EditorPlayState::Playing) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Only a playing Editor session can be paused");
    }
    m_snapshot.state = EditorPlayState::Paused;
    m_snapshot.accumulatorSeconds = 0.0;
    m_snapshot.stepPending = false;
    advanceRevision();
    return Core::success();
}

Core::Status EditorPlaySession::resume() noexcept
{
    if (m_snapshot.state != EditorPlayState::Paused) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Only a paused Editor session can resume");
    }
    m_snapshot.state = EditorPlayState::Playing;
    m_snapshot.stepPending = false;
    advanceRevision();
    return Core::success();
}

Core::Status EditorPlaySession::requestStep() noexcept
{
    if (m_snapshot.state != EditorPlayState::Paused) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor simulation stepping requires a paused session");
    }
    if (!m_snapshot.stepPending) {
        m_snapshot.stepPending = true;
        advanceRevision();
    }
    return Core::success();
}

Core::Result<Core::u32>
EditorPlaySession::advance(double frameDeltaSeconds) noexcept
{
    if (!active()) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor play session is not active");
    }
    if (!std::isfinite(frameDeltaSeconds) || frameDeltaSeconds < 0.0) {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor play frame delta must be finite and non-negative");
    }

    Core::u32 steps = 0;
    if (m_snapshot.state == EditorPlayState::Paused) {
        if (m_snapshot.stepPending) {
            steps = 1;
            m_snapshot.stepPending = false;
        }
    } else {
        const double boundedDelta =
            (std::min)(frameDeltaSeconds, m_config.maximumFrameDeltaSeconds);
        const double maximumAccumulator =
            m_config.fixedStepSeconds * m_config.maximumStepsPerFrame;
        m_snapshot.accumulatorSeconds =
            (std::min)(m_snapshot.accumulatorSeconds + boundedDelta,
                       maximumAccumulator);
        steps = static_cast<Core::u32>(
            m_snapshot.accumulatorSeconds / m_config.fixedStepSeconds);
        steps = (std::min)(steps, m_config.maximumStepsPerFrame);
        m_snapshot.accumulatorSeconds -=
            static_cast<double>(steps) * m_config.fixedStepSeconds;
    }

    if (steps != 0) {
        const Core::u64 maximum = (std::numeric_limits<Core::u64>::max)();
        const Core::u64 remaining = maximum - m_snapshot.simulationTickCount;
        m_snapshot.simulationTickCount += (std::min)(
            static_cast<Core::u64>(steps), remaining);
        m_snapshot.simulatedSeconds +=
            static_cast<double>(steps) * m_config.fixedStepSeconds;
        advanceRevision();
    }
    return steps;
}

Core::Status EditorPlaySession::stop() noexcept
{
    if (!active()) {
        return Core::success();
    }
    std::vector<std::byte>{}.swap(m_canonicalBytes);
    m_snapshot.state = EditorPlayState::Editing;
    m_snapshot.sourceDocumentRevision = 0;
    m_snapshot.simulationTickCount = 0;
    m_snapshot.simulatedSeconds = 0.0;
    m_snapshot.accumulatorSeconds = 0.0;
    m_snapshot.stepPending = false;
    advanceRevision();
    return Core::success();
}

void EditorPlaySession::advanceRevision() noexcept
{
    if (m_snapshot.revision != (std::numeric_limits<Core::u64>::max)()) {
        ++m_snapshot.revision;
    }
}

} // namespace Tina::Editor
