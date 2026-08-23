#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace Tina::Editor {

enum class EditorPlayWorkspace : Core::u8 {
    TwoD,
    ThreeD,
};

enum class EditorPlayState : Core::u8 {
    Editing,
    Playing,
    Paused,
};

struct EditorPlaySessionConfig final {
    // Maximum accepted canonical snapshot size. Storage is acquired by start()
    // for the actual payload and released by stop(); Create() does not reserve it.
    Core::usize canonicalByteCapacity = 16U * 1024U * 1024U;
    double fixedStepSeconds = 1.0 / 60.0;
    double maximumFrameDeltaSeconds = 0.25;
    Core::u32 maximumStepsPerFrame = 8;
};

struct EditorPlaySessionSnapshot final {
    EditorPlayWorkspace workspace = EditorPlayWorkspace::TwoD;
    EditorPlayState state = EditorPlayState::Editing;
    Core::u64 sourceDocumentRevision = 0;
    Core::u64 simulationTickCount = 0;
    double simulatedSeconds = 0.0;
    double accumulatorSeconds = 0.0;
    bool stepPending = false;
    Core::u64 revision = 1;
};

// Owns an isolated canonical document snapshot while active and a bounded
// fixed-step clock. The EditorApp remains responsible for instantiating the
// corresponding Scene world; authoring documents are never mutated by this
// session.
class EditorPlaySession final {
  public:
    EditorPlaySession(const EditorPlaySession&) = delete;
    EditorPlaySession& operator=(const EditorPlaySession&) = delete;
    EditorPlaySession(EditorPlaySession&&) noexcept = default;
    EditorPlaySession& operator=(EditorPlaySession&&) noexcept = default;

    [[nodiscard]] static Core::Result<EditorPlaySession>
    Create(EditorPlaySessionConfig config = {});

    [[nodiscard]] const EditorPlaySessionConfig& config() const noexcept
    {
        return m_config;
    }
    [[nodiscard]] const EditorPlaySessionSnapshot& snapshot() const noexcept
    {
        return m_snapshot;
    }
    [[nodiscard]] std::span<const std::byte> canonicalBytes() const noexcept
    {
        return m_canonicalBytes;
    }
    [[nodiscard]] bool active() const noexcept
    {
        return m_snapshot.state != EditorPlayState::Editing;
    }

    [[nodiscard]] Core::Status start(EditorPlayWorkspace workspace,
                                     Core::u64 sourceDocumentRevision,
                                     std::span<const std::byte> canonicalBytes);
    [[nodiscard]] Core::Status pause() noexcept;
    [[nodiscard]] Core::Status resume() noexcept;
    [[nodiscard]] Core::Status requestStep() noexcept;
    [[nodiscard]] Core::Result<Core::u32> advance(double frameDeltaSeconds) noexcept;
    [[nodiscard]] Core::Status stop() noexcept;

  private:
    explicit EditorPlaySession(EditorPlaySessionConfig config) noexcept;
    void advanceRevision() noexcept;

    EditorPlaySessionConfig m_config{};
    EditorPlaySessionSnapshot m_snapshot{};
    std::vector<std::byte> m_canonicalBytes{};
};

} // namespace Tina::Editor
