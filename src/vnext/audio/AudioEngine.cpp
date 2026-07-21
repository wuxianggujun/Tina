#include <tina/audio/AudioEngine.hpp>

#include <tina/audio/AudioErrors.hpp>
#include <tina/core/id/GenerationPool.hpp>

#include <exception>
#include <memory>
#include <new>
#include <string_view>
#include <thread>
#include <utility>

namespace Tina::Audio {
namespace {

[[nodiscard]] Core::Status fail(Core::ErrorCode code, std::string_view message) noexcept
{
    return Core::failure(code, message);
}

} // namespace

Core::Status validateAudioEngineConfig(const AudioEngineConfig& config) noexcept
{
    if (config.voiceCapacity == 0)
    {
        return fail(AudioErrorCode::InvalidConfiguration, "AudioEngine voiceCapacity must be greater than zero");
    }
    if (config.commandCapacity == 0 || config.completionCapacity == 0)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "AudioEngine command/completion capacity must be greater than zero");
    }
    return Core::success();
}

struct AudioEngine::Impl final {
    struct VoiceRecord final {
        AudioVoiceId id{};
    };

    using VoicePool = Core::GenerationPool<VoiceRecord, Detail::AudioVoiceRegistryTag>;

    Impl(AudioEngineConfig engineConfig, VoicePool voicePool, std::thread::id ownerThread) noexcept
        : config(engineConfig), voices(std::move(voicePool)), owner(ownerThread), state(AudioEngineState::Disabled)
    {
    }

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }

    [[nodiscard]] Core::Status requireOpenOwner() const noexcept
    {
        if (!isOwnerThread())
        {
            return fail(AudioErrorCode::WrongOwnerThread, "AudioEngine API must run on the owner thread");
        }
        if (state == AudioEngineState::Stopped || closed)
        {
            return fail(AudioErrorCode::EngineClosed, "AudioEngine is closed");
        }
        return Core::success();
    }

    AudioEngineConfig config{};
    VoicePool voices;
    std::thread::id owner{};
    AudioEngineState state = AudioEngineState::Uninitialized;
    bool closed = false;
};

Core::Result<AudioEngine> AudioEngine::Create(AudioEngineConfig config, std::pmr::memory_resource& resource)
{
    if (Core::Status status = validateAudioEngineConfig(config); !status)
    {
        return Core::failure(status.error());
    }

    auto voices = Impl::VoicePool::Create(config.voiceCapacity, resource);
    if (!voices)
    {
        if (voices.error().code == Core::CoreErrorCode::OutOfMemory)
        {
            return Core::failure(AudioErrorCode::ConstructionFailed, "AudioEngine voice pool allocation failed");
        }
        if (voices.error().code == Core::CoreErrorCode::CapacityExceeded ||
            voices.error().code == Core::CoreErrorCode::InvalidArgument)
        {
            return Core::failure(AudioErrorCode::InvalidConfiguration, voices.error().message);
        }
        return Core::failure(AudioErrorCode::ConstructionFailed, voices.error().message);
    }

    try
    {
        auto* impl = new Impl(config, std::move(*voices), std::this_thread::get_id());
        return AudioEngine(impl);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AudioErrorCode::ConstructionFailed, "AudioEngine allocation failed");
    }
    catch (const std::exception& exception)
    {
        return Core::failure(AudioErrorCode::ConstructionFailed, std::string_view(exception.what()));
    }
    catch (...)
    {
        return Core::failure(AudioErrorCode::ConstructionFailed, "AudioEngine allocation failed");
    }
}

AudioEngine::AudioEngine(Impl* impl) noexcept : m_impl(impl) {}

AudioEngine::~AudioEngine() noexcept
{
    shutdown();
    delete m_impl;
    m_impl = nullptr;
}

AudioEngine::AudioEngine(AudioEngine&& other) noexcept : m_impl(std::exchange(other.m_impl, nullptr)) {}

AudioEngineState AudioEngine::state() const noexcept
{
    if (m_impl == nullptr)
    {
        return AudioEngineState::Stopped;
    }
    return m_impl->state;
}

Core::Result<AudioEngineStats> AudioEngine::stats() const noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (!m_impl->isOwnerThread())
    {
        return Core::failure(AudioErrorCode::WrongOwnerThread, "AudioEngine API must run on the owner thread");
    }
    return AudioEngineStats{
        .voiceCapacity = m_impl->config.voiceCapacity,
        .liveVoices = m_impl->voices.activeCount(),
        .commandCapacity = m_impl->config.commandCapacity,
        .completionCapacity = m_impl->config.completionCapacity,
    };
}

Core::Result<AudioVoiceId> AudioEngine::createVoice() noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (Core::Status status = m_impl->requireOpenOwner(); !status)
    {
        return Core::failure(status.error());
    }

    auto voice = m_impl->voices.tryEmplace();
    if (!voice)
    {
        if (voice.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            return Core::failure(AudioErrorCode::CapacityExceeded, "AudioEngine voice capacity exceeded");
        }
        return Core::failure(AudioErrorCode::ConstructionFailed, voice.error().message);
    }

    if (auto* record = m_impl->voices.tryGet(*voice))
    {
        record->id = *voice;
    }
    return *voice;
}

Core::Status AudioEngine::destroyVoice(AudioVoiceId voice) noexcept
{
    if (m_impl == nullptr)
    {
        return fail(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (Core::Status status = m_impl->requireOpenOwner(); !status)
    {
        return status;
    }
    if (!voice.hasValue())
    {
        return fail(AudioErrorCode::InvalidVoice, "AudioVoiceId is empty");
    }

    switch (m_impl->voices.erase(voice))
    {
    case Core::GenerationEraseResult::Erased:
        return Core::success();
    case Core::GenerationEraseResult::WrongOwner:
        return fail(AudioErrorCode::InvalidVoice, "AudioVoiceId belongs to another engine");
    case Core::GenerationEraseResult::Stale:
        return fail(AudioErrorCode::StaleVoice, "AudioVoiceId generation is stale");
    case Core::GenerationEraseResult::OutOfRange:
    case Core::GenerationEraseResult::InvalidId:
    default:
        return fail(AudioErrorCode::InvalidVoice, "AudioVoiceId is invalid");
    }
}

Core::Result<bool> AudioEngine::isVoiceLive(AudioVoiceId voice) const noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (!m_impl->isOwnerThread())
    {
        return Core::failure(AudioErrorCode::WrongOwnerThread, "AudioEngine API must run on the owner thread");
    }
    if (m_impl->closed || m_impl->state == AudioEngineState::Stopped)
    {
        return false;
    }
    if (!voice.hasValue())
    {
        return false;
    }
    return m_impl->voices.tryGet(voice) != nullptr;
}

Core::Result<Core::u32> AudioEngine::pumpCompletions(Core::u32 budget) noexcept
{
    static_cast<void>(budget);
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (Core::Status status = m_impl->requireOpenOwner(); !status)
    {
        return Core::failure(status.error());
    }
    // M11-A7: no completion queue yet.
    return Core::u32{0};
}

void AudioEngine::shutdown() noexcept
{
    if (m_impl == nullptr || m_impl->closed)
    {
        return;
    }
    m_impl->state = AudioEngineState::Stopping;
    m_impl->voices.clear();
    m_impl->state = AudioEngineState::Stopped;
    m_impl->closed = true;
}

} // namespace Tina::Audio
