#include <tina/audio/AudioEngine.hpp>

#include <tina/audio/AudioErrors.hpp>
#include <tina/core/id/GenerationPool.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
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
    // Realtime-readable mix slot. Main thread publishes; mixRealtime advances cursor.
    struct MixSlot final {
        std::atomic<const float*> frames{nullptr};
        std::atomic<Core::u64> frameCount{0};
        std::atomic<Core::u32> channels{0};
        std::atomic<Core::u32> sampleRate{0};
        std::atomic<Core::u64> cursor{0};
        std::atomic<float> gain{1.0F};
        std::atomic<bool> active{false};
        std::atomic<bool> finished{false};
        AudioVoiceId voice{};
    };

    struct VoiceRecord final {
        AudioVoiceId id{};
        bool playing = false;
        bool hasClip = false;
        AudioPcmClipView clip{};
        Core::u32 mixSlot = InvalidMixSlot;
    };

    using VoicePool = Core::GenerationPool<VoiceRecord, Detail::AudioVoiceRegistryTag>;
    static constexpr Core::u32 InvalidMixSlot = (std::numeric_limits<Core::u32>::max)();
    static constexpr Core::usize MaxMixSlots = 32;

    Impl(AudioEngineConfig engineConfig, VoicePool voicePool, FixedRing<AudioCommand> commandRing,
         FixedRing<AudioCompletion> completionRing, std::thread::id ownerThread) noexcept
        : config(engineConfig),
          voices(std::move(voicePool)),
          commands(std::move(commandRing)),
          completions(std::move(completionRing)),
          owner(ownerThread),
          state(AudioEngineState::Disabled)
    {
        for (auto& slot : mixSlots)
        {
            slot.active.store(false, std::memory_order_relaxed);
            slot.finished.store(false, std::memory_order_relaxed);
            slot.frames.store(nullptr, std::memory_order_relaxed);
            slot.frameCount.store(0, std::memory_order_relaxed);
            slot.channels.store(0, std::memory_order_relaxed);
            slot.sampleRate.store(0, std::memory_order_relaxed);
            slot.cursor.store(0, std::memory_order_relaxed);
            slot.gain.store(1.0F, std::memory_order_relaxed);
            slot.voice = {};
        }
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
                if (!record->hasClip || record->clip.empty())
                {
                    record->playing = false;
                    deactivateMixSlot(*record);
                    pushCompletion(AudioCompletionKind::RejectedNoClip, command.voice, command.sequence);
                    break;
                }
                if (!activateMixSlot(*record, computeSfxGain()))
                {
                    record->playing = false;
                    pushCompletion(AudioCompletionKind::RejectedNoClip, command.voice, command.sequence);
                    break;
                }
                record->playing = true;
                pushCompletion(AudioCompletionKind::Started, command.voice, command.sequence);
                break;
            case AudioCommandKind::Stop:
                record->playing = false;
                deactivateMixSlot(*record);
                pushCompletion(AudioCompletionKind::Stopped, command.voice, command.sequence);
                break;
            }
        }
    }

    [[nodiscard]] float computeSfxGain() const noexcept
    {
        const AudioBusState& master = buses[static_cast<Core::usize>(AudioBusId::Master)];
        const AudioBusState& sfx = buses[static_cast<Core::usize>(AudioBusId::Sfx)];
        if (master.muted || sfx.muted)
        {
            return 0.0F;
        }
        return master.volume * sfx.volume;
    }

    void publishBusGainToActiveSlots() noexcept
    {
        const float gain = computeSfxGain();
        for (auto& slot : mixSlots)
        {
            if (slot.active.load(std::memory_order_relaxed))
            {
                slot.gain.store(gain, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] bool activateMixSlot(VoiceRecord& record, float gain) noexcept
    {
        deactivateMixSlot(record);
        for (Core::u32 index = 0; index < MaxMixSlots; ++index)
        {
            MixSlot& slot = mixSlots[index];
            if (slot.active.load(std::memory_order_relaxed))
            {
                continue;
            }
            slot.voice = record.id;
            slot.frames.store(record.clip.frames, std::memory_order_relaxed);
            slot.frameCount.store(record.clip.frameCount, std::memory_order_relaxed);
            slot.channels.store(record.clip.channels, std::memory_order_relaxed);
            slot.sampleRate.store(record.clip.sampleRate, std::memory_order_relaxed);
            slot.cursor.store(0, std::memory_order_relaxed);
            slot.gain.store(gain, std::memory_order_relaxed);
            slot.finished.store(false, std::memory_order_relaxed);
            slot.active.store(true, std::memory_order_release);
            record.mixSlot = index;
            return true;
        }
        return false;
    }

    void deactivateMixSlot(VoiceRecord& record) noexcept
    {
        if (record.mixSlot == InvalidMixSlot || record.mixSlot >= MaxMixSlots)
        {
            record.mixSlot = InvalidMixSlot;
            return;
        }
        MixSlot& slot = mixSlots[record.mixSlot];
        slot.active.store(false, std::memory_order_release);
        slot.finished.store(false, std::memory_order_relaxed);
        slot.frames.store(nullptr, std::memory_order_relaxed);
        slot.cursor.store(0, std::memory_order_relaxed);
        slot.voice = {};
        record.mixSlot = InvalidMixSlot;
    }

    void harvestNaturalEnds() noexcept
    {
        for (Core::u32 index = 0; index < MaxMixSlots; ++index)
        {
            MixSlot& slot = mixSlots[index];
            if (!slot.finished.load(std::memory_order_acquire))
            {
                continue;
            }
            slot.finished.store(false, std::memory_order_relaxed);
            slot.active.store(false, std::memory_order_release);
            const AudioVoiceId voice = slot.voice;
            slot.voice = {};
            slot.frames.store(nullptr, std::memory_order_relaxed);
            if (!voice.hasValue())
            {
                continue;
            }
            if (auto* record = voices.tryGet(voice); record != nullptr)
            {
                record->playing = false;
                record->mixSlot = InvalidMixSlot;
            }
            pushCompletion(AudioCompletionKind::Stopped, voice, 0);
        }
    }

    void mixRealtime(float* interleavedOut, Core::u32 outFrames, Core::u32 outChannels,
                     Core::u32 outSampleRate) noexcept
    {
        if (interleavedOut == nullptr || outFrames == 0 || (outChannels != 1 && outChannels != 2) ||
            outSampleRate == 0)
        {
            return;
        }
        const Core::usize sampleCount = static_cast<Core::usize>(outFrames) * outChannels;
        std::memset(interleavedOut, 0, sampleCount * sizeof(float));
        if (closed || state == AudioEngineState::Stopped)
        {
            return;
        }

        for (auto& slot : mixSlots)
        {
            if (!slot.active.load(std::memory_order_acquire))
            {
                continue;
            }
            const float* frames = slot.frames.load(std::memory_order_relaxed);
            const Core::u64 frameCount = slot.frameCount.load(std::memory_order_relaxed);
            const Core::u32 channels = slot.channels.load(std::memory_order_relaxed);
            const Core::u32 sampleRate = slot.sampleRate.load(std::memory_order_relaxed);
            if (frames == nullptr || frameCount == 0 || channels == 0 || sampleRate == 0)
            {
                continue;
            }
            Core::u64 cursor = slot.cursor.load(std::memory_order_relaxed);
            if (cursor >= frameCount)
            {
                slot.active.store(false, std::memory_order_release);
                slot.finished.store(true, std::memory_order_release);
                continue;
            }
            const float gain = slot.gain.load(std::memory_order_relaxed);
            // A12: same-rate mix only; mismatched rates are muted (no allocation resampler).
            if (sampleRate != outSampleRate)
            {
                continue;
            }
            for (Core::u32 f = 0; f < outFrames; ++f)
            {
                if (cursor >= frameCount)
                {
                    slot.active.store(false, std::memory_order_release);
                    slot.finished.store(true, std::memory_order_release);
                    break;
                }
                const Core::u64 base = cursor * channels;
                if (outChannels == 1)
                {
                    float sample = frames[base] * gain;
                    if (channels >= 2)
                    {
                        sample = 0.5F * (frames[base] + frames[base + 1]) * gain;
                    }
                    interleavedOut[f] += sample;
                }
                else
                {
                    const float left = frames[base] * gain;
                    const float right = channels >= 2 ? frames[base + 1] * gain : left;
                    interleavedOut[static_cast<Core::usize>(f) * 2U] += left;
                    interleavedOut[static_cast<Core::usize>(f) * 2U + 1U] += right;
                }
                ++cursor;
            }
            slot.cursor.store(cursor, std::memory_order_relaxed);
            if (cursor >= frameCount)
            {
                slot.active.store(false, std::memory_order_release);
                slot.finished.store(true, std::memory_order_release);
            }
        }
        mixFramesRendered += outFrames;
    }

    [[nodiscard]] Core::usize countActiveMixSlots() const noexcept
    {
        Core::usize count = 0;
        for (const auto& slot : mixSlots)
        {
            if (slot.active.load(std::memory_order_relaxed))
            {
                ++count;
            }
        }
        return count;
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
            case AudioCompletionKind::RejectedNoClip:
                ++completedRejectedNoClip;
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

    [[nodiscard]] static Core::Status validateClipView(const AudioPcmClipView& clip) noexcept
    {
        if (clip.empty())
        {
            return fail(AudioErrorCode::InvalidConfiguration, "AudioPcmClipView is empty");
        }
        if (clip.channels == 0 || clip.channels > 8)
        {
            return fail(AudioErrorCode::InvalidConfiguration, "AudioPcmClipView channels must be in [1, 8]");
        }
        if (clip.sampleRate < 1000 || clip.sampleRate > 192000)
        {
            return fail(AudioErrorCode::InvalidConfiguration, "AudioPcmClipView sampleRate out of range");
        }
        return Core::success();
    }

    [[nodiscard]] Core::usize countBoundClips() const noexcept
    {
        // GenerationPool has no full scan iterator; track on bind/clear instead.
        return boundClipVoices;
    }

    AudioEngineConfig config{};
    VoicePool voices;
    FixedRing<AudioCommand> commands{};
    FixedRing<AudioCompletion> completions{};
    std::array<AudioBusState, AudioBusCount> buses{};
    std::array<MixSlot, MaxMixSlots> mixSlots{};
    std::thread::id owner{};
    AudioEngineState state = AudioEngineState::Uninitialized;
    bool closed = false;
    Core::u64 nextCommandSequence = 0;
    Core::usize rejectedCommands = 0;
    Core::usize completedStarted = 0;
    Core::usize completedStopped = 0;
    Core::usize completedRejectedStale = 0;
    Core::usize completedRejectedNoClip = 0;
    Core::usize boundClipVoices = 0;
    Core::u64 mixFramesRendered = 0;
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

AudioEngine& AudioEngine::operator=(AudioEngine&& other) noexcept
{
    if (this != &other)
    {
        shutdown();
        delete m_impl;
        m_impl = std::exchange(other.m_impl, nullptr);
    }
    return *this;
}

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
        .completedRejectedNoClip = m_impl->completedRejectedNoClip,
        .boundClipVoices = m_impl->boundClipVoices,
        .activeMixVoices = m_impl->countActiveMixSlots(),
        .mixFramesRendered = m_impl->mixFramesRendered,
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
        record->hasClip = false;
        record->clip = {};
        record->mixSlot = Impl::InvalidMixSlot;
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

    if (auto* record = m_impl->voices.tryGet(voice); record != nullptr)
    {
        if (record->hasClip && m_impl->boundClipVoices > 0)
        {
            --m_impl->boundClipVoices;
        }
        m_impl->deactivateMixSlot(*record);
        record->playing = false;
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

Core::Status AudioEngine::bindVoiceClip(AudioVoiceId voice, AudioPcmClipView clip) noexcept
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
    if (Core::Status status = Impl::validateClipView(clip); !status)
    {
        return status;
    }
    auto* record = m_impl->voices.tryGet(voice);
    if (record == nullptr)
    {
        return fail(AudioErrorCode::StaleVoice, "AudioVoiceId is not live");
    }
    if (record->playing)
    {
        return fail(AudioErrorCode::InvalidConfiguration, "Cannot rebind clip while voice is playing");
    }
    const bool hadClip = record->hasClip;
    record->clip = clip;
    record->hasClip = true;
    if (!hadClip)
    {
        ++m_impl->boundClipVoices;
    }
    return Core::success();
}

Core::Status AudioEngine::clearVoiceClip(AudioVoiceId voice) noexcept
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
    auto* record = m_impl->voices.tryGet(voice);
    if (record == nullptr)
    {
        return fail(AudioErrorCode::StaleVoice, "AudioVoiceId is not live");
    }
    if (record->playing)
    {
        return fail(AudioErrorCode::InvalidConfiguration, "Cannot clear clip while voice is playing");
    }
    if (record->hasClip)
    {
        record->hasClip = false;
        record->clip = {};
        if (m_impl->boundClipVoices > 0)
        {
            --m_impl->boundClipVoices;
        }
    }
    return Core::success();
}

Core::Result<AudioPcmClipView> AudioEngine::voiceClip(AudioVoiceId voice) const noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (!m_impl->isOwnerThread())
    {
        return Core::failure(AudioErrorCode::WrongOwnerThread, "AudioEngine API must run on the owner thread");
    }
    if (!voice.hasValue())
    {
        return Core::failure(AudioErrorCode::InvalidVoice, "AudioVoiceId is empty");
    }
    const auto* record = m_impl->voices.tryGet(voice);
    if (record == nullptr)
    {
        return Core::failure(AudioErrorCode::StaleVoice, "AudioVoiceId is not live");
    }
    if (!record->hasClip)
    {
        return AudioPcmClipView{};
    }
    return record->clip;
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
    m_impl->publishBusGainToActiveSlots();
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
    m_impl->publishBusGainToActiveSlots();
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

Core::Result<AudioVoiceId> AudioEngine::playOneShotPcm(AudioPcmClipView clip) noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (Core::Status status = m_impl->requireOpenOwner(); !status)
    {
        return Core::failure(status.error());
    }
    auto voice = createVoice();
    if (!voice)
    {
        return Core::failure(voice.error());
    }
    if (Core::Status status = bindVoiceClip(*voice, clip); !status)
    {
        (void)destroyVoice(*voice);
        return Core::failure(status.error());
    }
    if (Core::Status status = enqueuePlay(*voice); !status)
    {
        (void)clearVoiceClip(*voice);
        (void)destroyVoice(*voice);
        return Core::failure(status.error());
    }
    return *voice;
}

void AudioEngine::mixRealtime(float* interleavedOut, Core::u32 outFrames, Core::u32 outChannels,
                              Core::u32 outSampleRate) noexcept
{
    if (m_impl == nullptr)
    {
        if (interleavedOut != nullptr && outFrames > 0 && outChannels > 0)
        {
            std::memset(interleavedOut,
                        0,
                        static_cast<Core::usize>(outFrames) * outChannels * sizeof(float));
        }
        return;
    }
    m_impl->mixRealtime(interleavedOut, outFrames, outChannels, outSampleRate);
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
    m_impl->harvestNaturalEnds();
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
    m_impl->harvestNaturalEnds();
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
    for (auto& slot : m_impl->mixSlots)
    {
        slot.active.store(false, std::memory_order_relaxed);
        slot.finished.store(false, std::memory_order_relaxed);
        slot.frames.store(nullptr, std::memory_order_relaxed);
        slot.voice = {};
    }
    m_impl->voices.clear();
    m_impl->boundClipVoices = 0;
    m_impl->state = AudioEngineState::Stopped;
    m_impl->closed = true;
}

} // namespace Tina::Audio
