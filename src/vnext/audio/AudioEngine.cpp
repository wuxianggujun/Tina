#include <tina/audio/AudioEngine.hpp>

#include <tina/audio/AudioErrors.hpp>
#include <tina/core/id/GenerationPool.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
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

template <typename T>
struct FixedRing final {
    T* storage = nullptr;
    Core::usize capacity = 0;
    Core::usize head = 0;
    Core::usize tail = 0;
    Core::usize count = 0;
    std::pmr::memory_resource* resource = nullptr;

    FixedRing() noexcept = default;
    FixedRing(const FixedRing&) = delete;
    FixedRing& operator=(const FixedRing&) = delete;

    FixedRing(FixedRing&& other) noexcept
        : storage(std::exchange(other.storage, nullptr)),
          capacity(std::exchange(other.capacity, 0)),
          head(std::exchange(other.head, 0)),
          tail(std::exchange(other.tail, 0)),
          count(std::exchange(other.count, 0)),
          resource(std::exchange(other.resource, nullptr))
    {
    }

    FixedRing& operator=(FixedRing&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            storage = std::exchange(other.storage, nullptr);
            capacity = std::exchange(other.capacity, 0);
            head = std::exchange(other.head, 0);
            tail = std::exchange(other.tail, 0);
            count = std::exchange(other.count, 0);
            resource = std::exchange(other.resource, nullptr);
        }
        return *this;
    }

    ~FixedRing() noexcept
    {
        destroy();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return count == 0;
    }

    [[nodiscard]] bool full() const noexcept
    {
        return capacity != 0 && count >= capacity;
    }

    [[nodiscard]] bool tryPush(const T& value) noexcept
    {
        if (storage == nullptr || full())
        {
            return false;
        }
        storage[tail] = value;
        tail = (tail + 1) % capacity;
        ++count;
        return true;
    }

    [[nodiscard]] bool tryPop(T& out) noexcept
    {
        if (storage == nullptr || empty())
        {
            return false;
        }
        out = storage[head];
        head = (head + 1) % capacity;
        --count;
        return true;
    }

    void clear() noexcept
    {
        head = 0;
        tail = 0;
        count = 0;
    }

    void destroy() noexcept
    {
        if (resource != nullptr && storage != nullptr && capacity > 0)
        {
            std::destroy_n(storage, capacity);
            resource->deallocate(storage, sizeof(T) * capacity, alignof(T));
        }
        storage = nullptr;
        capacity = 0;
        head = 0;
        tail = 0;
        count = 0;
        resource = nullptr;
    }
};

struct AudioCommand final {
    AudioCommandKind kind = AudioCommandKind::Stop;
    AudioVoiceId voice{};
    Core::u64 sequence = 0;
};

struct AudioCompletion final {
    AudioCompletionKind kind = AudioCompletionKind::Stopped;
    AudioVoiceId voice{};
    Core::u64 commandSequence = 0;
};

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
    constexpr Core::usize MaxRing = (std::numeric_limits<Core::usize>::max)() / 4;
    if (config.commandCapacity > MaxRing || config.completionCapacity > MaxRing)
    {
        return fail(AudioErrorCode::InvalidConfiguration, "AudioEngine ring capacity is unreasonably large");
    }
    return Core::success();
}

struct AudioEngine::Impl final {
    struct VoiceRecord final {
        AudioVoiceId id{};
        bool playing = false;
    };

    using VoicePool = Core::GenerationPool<VoiceRecord, Detail::AudioVoiceRegistryTag>;

    Impl(AudioEngineConfig engineConfig, VoicePool voicePool, FixedRing<AudioCommand> commandRing,
         FixedRing<AudioCompletion> completionRing, std::thread::id ownerThread) noexcept
        : config(engineConfig),
          voices(std::move(voicePool)),
          commands(std::move(commandRing)),
          completions(std::move(completionRing)),
          owner(ownerThread),
          state(AudioEngineState::Disabled)
    {
    }

    ~Impl() noexcept
    {
        commands.destroy();
        completions.destroy();
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

    [[nodiscard]] Core::Status enqueueCommand(AudioCommandKind kind, AudioVoiceId voice) noexcept
    {
        if (Core::Status status = requireOpenOwner(); !status)
        {
            return status;
        }
        if (!voice.hasValue())
        {
            return fail(AudioErrorCode::InvalidVoice, "AudioVoiceId is empty");
        }
        if (voices.tryGet(voice) == nullptr)
        {
            // Distinguish wrong-owner / stale via erase semantics without mutating:
            // tryGet nullptr covers invalid/stale/wrong-owner for this engine.
            return fail(AudioErrorCode::StaleVoice, "AudioVoiceId is not live");
        }
        if (commands.full())
        {
            ++rejectedCommands;
            return fail(AudioErrorCode::CapacityExceeded, "AudioEngine command queue is full");
        }
        const AudioCommand command{
            .kind = kind,
            .voice = voice,
            .sequence = ++nextCommandSequence,
        };
        if (!commands.tryPush(command))
        {
            ++rejectedCommands;
            return fail(AudioErrorCode::CapacityExceeded, "AudioEngine command queue is full");
        }
        return Core::success();
    }

    void pushCompletion(AudioCompletionKind kind, AudioVoiceId voice, Core::u64 sequence) noexcept
    {
        if (completions.full())
        {
            // Completions must not grow; count as rejected so tests can detect pressure.
            ++rejectedCommands;
            return;
        }
        const AudioCompletion completion{
            .kind = kind,
            .voice = voice,
            .commandSequence = sequence,
        };
        (void)completions.tryPush(completion);
    }

    void applyCommands() noexcept
    {
        AudioCommand command{};
        while (commands.tryPop(command))
        {
            VoiceRecord* record = voices.tryGet(command.voice);
            if (record == nullptr)
            {
                pushCompletion(AudioCompletionKind::RejectedStale, command.voice, command.sequence);
                continue;
            }
            switch (command.kind)
            {
            case AudioCommandKind::Play:
                record->playing = true;
                pushCompletion(AudioCompletionKind::Started, command.voice, command.sequence);
                break;
            case AudioCommandKind::Stop:
                record->playing = false;
                pushCompletion(AudioCompletionKind::Stopped, command.voice, command.sequence);
                break;
            }
        }
    }

    [[nodiscard]] Core::u32 drainCompletions(std::span<AudioCompletionEvent> out, Core::u32 budget) noexcept
    {
        Core::u32 limit = budget == 0 ? static_cast<Core::u32>(out.size()) : budget;
        limit = static_cast<Core::u32>((std::min)(static_cast<Core::usize>(limit), out.size()));
        Core::u32 written = 0;
        AudioCompletion completion{};
        while (written < limit && completions.tryPop(completion))
        {
            out[written] = AudioCompletionEvent{
                .kind = completion.kind,
                .voice = completion.voice,
                .commandSequence = completion.commandSequence,
            };
            switch (completion.kind)
            {
            case AudioCompletionKind::Started:
                ++completedStarted;
                break;
            case AudioCompletionKind::Stopped:
                ++completedStopped;
                break;
            case AudioCompletionKind::RejectedStale:
                ++completedRejectedStale;
                break;
            }
            ++written;
        }
        return written;
    }

    [[nodiscard]] static bool isValidBus(AudioBusId bus) noexcept
    {
        return static_cast<Core::u8>(bus) < static_cast<Core::u8>(AudioBusCount);
    }

    AudioEngineConfig config{};
    VoicePool voices;
    FixedRing<AudioCommand> commands{};
    FixedRing<AudioCompletion> completions{};
    std::array<AudioBusState, AudioBusCount> buses{};
    std::thread::id owner{};
    AudioEngineState state = AudioEngineState::Uninitialized;
    bool closed = false;
    Core::u64 nextCommandSequence = 0;
    Core::usize rejectedCommands = 0;
    Core::usize completedStarted = 0;
    Core::usize completedStopped = 0;
    Core::usize completedRejectedStale = 0;
};

namespace {

template <typename T>
[[nodiscard]] Core::Result<FixedRing<T>> allocateRing(Core::usize capacity,
                                                      std::pmr::memory_resource& resource) noexcept
{
    FixedRing<T> ring{};
    try
    {
        void* raw = resource.allocate(sizeof(T) * capacity, alignof(T));
        ring.storage = static_cast<T*>(raw);
        ring.capacity = capacity;
        ring.resource = &resource;
        std::uninitialized_default_construct_n(ring.storage, capacity);
        return ring;
    }
    catch (const std::bad_alloc&)
    {
        ring.destroy();
        return Core::failure(AudioErrorCode::ConstructionFailed, "AudioEngine ring allocation failed");
    }
    catch (const std::exception& exception)
    {
        ring.destroy();
        return Core::failure(AudioErrorCode::ConstructionFailed, std::string_view(exception.what()));
    }
    catch (...)
    {
        ring.destroy();
        return Core::failure(AudioErrorCode::ConstructionFailed, "AudioEngine ring allocation failed");
    }
}

} // namespace

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

    auto commandRing = allocateRing<AudioCommand>(config.commandCapacity, resource);
    if (!commandRing)
    {
        return Core::failure(commandRing.error());
    }
    auto completionRing = allocateRing<AudioCompletion>(config.completionCapacity, resource);
    if (!completionRing)
    {
        commandRing->destroy();
        return Core::failure(completionRing.error());
    }

    try
    {
        auto* impl = new Impl(config, std::move(*voices), std::move(*commandRing), std::move(*completionRing),
                              std::this_thread::get_id());
        return AudioEngine(impl);
    }
    catch (const std::bad_alloc&)
    {
        // Rings/voices may have been moved; moved-from FixedRing is empty.
        if (commandRing)
        {
            commandRing->destroy();
        }
        if (completionRing)
        {
            completionRing->destroy();
        }
        return Core::failure(AudioErrorCode::ConstructionFailed, "AudioEngine allocation failed");
    }
    catch (const std::exception& exception)
    {
        if (commandRing)
        {
            commandRing->destroy();
        }
        if (completionRing)
        {
            completionRing->destroy();
        }
        return Core::failure(AudioErrorCode::ConstructionFailed, std::string_view(exception.what()));
    }
    catch (...)
    {
        if (commandRing)
        {
            commandRing->destroy();
        }
        if (completionRing)
        {
            completionRing->destroy();
        }
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
        .pendingCommands = m_impl->commands.count,
        .pendingCompletions = m_impl->completions.count,
        .rejectedCommands = m_impl->rejectedCommands,
        .completedStarted = m_impl->completedStarted,
        .completedStopped = m_impl->completedStopped,
        .completedRejectedStale = m_impl->completedRejectedStale,
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
        record->playing = false;
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

Core::Result<bool> AudioEngine::isVoicePlaying(AudioVoiceId voice) const noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (!m_impl->isOwnerThread())
    {
        return Core::failure(AudioErrorCode::WrongOwnerThread, "AudioEngine API must run on the owner thread");
    }
    if (m_impl->closed || m_impl->state == AudioEngineState::Stopped || !voice.hasValue())
    {
        return false;
    }
    const auto* record = m_impl->voices.tryGet(voice);
    if (record == nullptr)
    {
        return false;
    }
    return record->playing;
}

Core::Status AudioEngine::setBusVolume(AudioBusId bus, float volume) noexcept
{
    if (m_impl == nullptr)
    {
        return fail(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (Core::Status status = m_impl->requireOpenOwner(); !status)
    {
        return status;
    }
    if (!Impl::isValidBus(bus))
    {
        return fail(AudioErrorCode::InvalidConfiguration, "AudioBusId is out of range");
    }
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F)
    {
        return fail(AudioErrorCode::InvalidConfiguration, "Audio bus volume must be finite in [0, 1]");
    }
    m_impl->buses[static_cast<Core::usize>(bus)].volume = volume;
    return Core::success();
}

Core::Status AudioEngine::setBusMuted(AudioBusId bus, bool muted) noexcept
{
    if (m_impl == nullptr)
    {
        return fail(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (Core::Status status = m_impl->requireOpenOwner(); !status)
    {
        return status;
    }
    if (!Impl::isValidBus(bus))
    {
        return fail(AudioErrorCode::InvalidConfiguration, "AudioBusId is out of range");
    }
    m_impl->buses[static_cast<Core::usize>(bus)].muted = muted;
    return Core::success();
}

Core::Result<AudioBusState> AudioEngine::busState(AudioBusId bus) const noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (!m_impl->isOwnerThread())
    {
        return Core::failure(AudioErrorCode::WrongOwnerThread, "AudioEngine API must run on the owner thread");
    }
    if (!Impl::isValidBus(bus))
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "AudioBusId is out of range");
    }
    return m_impl->buses[static_cast<Core::usize>(bus)];
}

Core::Result<float> AudioEngine::effectiveBusGain(AudioBusId bus) const noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (!m_impl->isOwnerThread())
    {
        return Core::failure(AudioErrorCode::WrongOwnerThread, "AudioEngine API must run on the owner thread");
    }
    if (!Impl::isValidBus(bus))
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "AudioBusId is out of range");
    }
    const AudioBusState& master = m_impl->buses[static_cast<Core::usize>(AudioBusId::Master)];
    const AudioBusState& target = m_impl->buses[static_cast<Core::usize>(bus)];
    if (master.muted || target.muted)
    {
        return 0.0F;
    }
    if (bus == AudioBusId::Master)
    {
        return master.volume;
    }
    return master.volume * target.volume;
}

Core::Status AudioEngine::enqueuePlay(AudioVoiceId voice) noexcept
{
    if (m_impl == nullptr)
    {
        return fail(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    return m_impl->enqueueCommand(AudioCommandKind::Play, voice);
}

Core::Status AudioEngine::enqueueStop(AudioVoiceId voice) noexcept
{
    if (m_impl == nullptr)
    {
        return fail(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    return m_impl->enqueueCommand(AudioCommandKind::Stop, voice);
}

Core::Result<Core::u32> AudioEngine::pumpCompletions(std::span<AudioCompletionEvent> out, Core::u32 budget) noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (Core::Status status = m_impl->requireOpenOwner(); !status)
    {
        return Core::failure(status.error());
    }
    m_impl->applyCommands();
    return m_impl->drainCompletions(out, budget);
}

Core::Result<Core::u32> AudioEngine::pumpCompletions(Core::u32 budget) noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (Core::Status status = m_impl->requireOpenOwner(); !status)
    {
        return Core::failure(status.error());
    }
    m_impl->applyCommands();
    // Drain without delivering events (tests that only care about stats/counters).
    AudioCompletionEvent scratch[16]{};
    Core::u32 total = 0;
    for (;;)
    {
        const Core::u32 remaining =
            budget == 0 ? static_cast<Core::u32>((std::numeric_limits<Core::u32>::max)() - total)
                        : (budget > total ? budget - total : 0);
        if (budget != 0 && remaining == 0)
        {
            break;
        }
        const Core::u32 chunkBudget =
            budget == 0 ? static_cast<Core::u32>(std::size(scratch))
                        : (std::min)(remaining, static_cast<Core::u32>(std::size(scratch)));
        const Core::u32 written =
            m_impl->drainCompletions(std::span<AudioCompletionEvent>{scratch}, chunkBudget);
        total += written;
        if (written == 0)
        {
            break;
        }
    }
    return total;
}

void AudioEngine::shutdown() noexcept
{
    if (m_impl == nullptr || m_impl->closed)
    {
        return;
    }
    m_impl->state = AudioEngineState::Stopping;
    m_impl->commands.clear();
    m_impl->completions.clear();
    m_impl->voices.clear();
    m_impl->state = AudioEngineState::Stopped;
    m_impl->closed = true;
}

} // namespace Tina::Audio
