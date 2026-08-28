#include <tina/audio/AudioEngine.hpp>

#include <tina/audio/AudioErrors.hpp>
#include <tina/core/id/GenerationPool.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
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

template <typename T>
struct FixedStorage final {
    T* storage = nullptr;
    Core::usize count = 0;
    std::pmr::memory_resource* resource = nullptr;

    FixedStorage() noexcept = default;
    FixedStorage(const FixedStorage&) = delete;
    FixedStorage& operator=(const FixedStorage&) = delete;

    FixedStorage(FixedStorage&& other) noexcept
        : storage(std::exchange(other.storage, nullptr)),
          count(std::exchange(other.count, 0)),
          resource(std::exchange(other.resource, nullptr))
    {
    }

    FixedStorage& operator=(FixedStorage&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            storage = std::exchange(other.storage, nullptr);
            count = std::exchange(other.count, 0);
            resource = std::exchange(other.resource, nullptr);
        }
        return *this;
    }

    ~FixedStorage() noexcept
    {
        destroy();
    }

    void destroy() noexcept
    {
        if (resource != nullptr && storage != nullptr && count > 0)
        {
            std::destroy_n(storage, count);
            resource->deallocate(storage, sizeof(T) * count, alignof(T));
        }
        storage = nullptr;
        count = 0;
        resource = nullptr;
    }
};

enum class VoiceGainControlKind : Core::u8 {
    None = 0,
    SetGain = 1,
    StartFade = 2,
    CancelFade = 3,
};

enum class VoiceSourceKind : Core::u8 {
    None = 0,
    Clip = 1,
    Stream = 2,
};

} // namespace

Core::Status validateAudioEngineConfig(const AudioEngineConfig& config) noexcept
{
    if (config.voiceCapacity == 0)
    {
        return fail(AudioErrorCode::InvalidConfiguration, "AudioEngine voiceCapacity must be greater than zero");
    }
    if (config.voiceCapacity > AudioMaxRealtimeVoices)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "AudioEngine voiceCapacity exceeds AudioMaxRealtimeVoices");
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
    if (config.streamBufferFrameCapacity < AudioPcmStreamMinBufferFrames)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "AudioEngine streamBufferFrameCapacity must be at least two frames");
    }
    constexpr Core::usize StreamChannels = AudioPcmStreamMaxChannels;
    if (config.streamBufferFrameCapacity >
        (std::numeric_limits<Core::usize>::max)() / StreamChannels / config.voiceCapacity / sizeof(float))
    {
        return fail(AudioErrorCode::InvalidConfiguration, "AudioEngine stream buffer storage size overflowed");
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
        std::atomic<bool> streaming{false};
        std::atomic<Core::u32> streamIndex{0};
        std::atomic<Core::u64> cursorFrame{0};
        std::atomic<Core::u32> cursorFraction{0};
        std::atomic<float> busGain{1.0F};
        std::atomic<float> pitch{1.0F};
        std::atomic<float> pan{0.0F};

        // Single-owner control publication. Odd revision means payload write in
        // progress; the callback never spins and defers it to the next block.
        std::atomic<Core::u64> gainControlRevision{0};
        std::atomic<Core::u8> gainControlKind{static_cast<Core::u8>(VoiceGainControlKind::None)};
        std::atomic<float> gainControlTarget{1.0F};
        std::atomic<Core::u64> gainControlDurationBits{0};
        std::atomic<Core::u8> gainControlEndAction{static_cast<Core::u8>(AudioFadeEndAction::KeepPlaying)};
        std::atomic<Core::u64> appliedGainControlRevision{0};

        // Callback-owned ramp state, atomically mirrored for owner-thread query
        // and safe slot reset. The callback loads once and stores once per block.
        std::atomic<float> currentVoiceGain{1.0F};
        std::atomic<float> fadeStartGain{1.0F};
        std::atomic<float> fadeTargetGain{1.0F};
        std::atomic<Core::u64> fadeTotalFrames{0};
        std::atomic<Core::u64> fadeElapsedFrames{0};
        std::atomic<Core::u8> fadeEndAction{static_cast<Core::u8>(AudioFadeEndAction::KeepPlaying)};
        std::atomic<bool> fadeActive{false};
        // Owner publishes a new epoch whenever slot ownership changes. Realtime
        // readers only snapshot it and publish the epoch that reached terminal.
        std::atomic<Core::u64> publicationGeneration{1};
        std::atomic<Core::u32> callbackReaders{0};
        std::atomic<bool> active{false};
        std::atomic<Core::u64> finishedPublicationGeneration{0};
        AudioVoiceId voice{};
    };

    struct StreamSlot final {
        float* frames = nullptr;
        Core::usize reservedCapacityFrames = 0;
        Core::usize capacityFrames = 0;
        Core::u32 channels = 0;
        Core::u32 sampleRate = 0;
        bool configured = false;
        bool terminalCompletionPending = false;
        AudioCompletionKind terminalCompletion = AudioCompletionKind::Stopped;
        AudioVoiceId terminalVoice{};
        Core::u64 terminalCommandSequence = 0;
        Core::u32 quiescingMixSlot = InvalidMixSlot;
        Core::u64 quiescingPublicationGeneration = 0;
        std::atomic<Core::u64> readFrame{0};
        std::atomic<Core::u64> writeFrame{0};
        std::atomic<Core::u64> underrunFrames{0};
        std::atomic<bool> eofSignaled{false};
    };

    struct VoiceRecord final {
        AudioVoiceId id{};
        bool playing = false;
        bool hasClip = false;
        bool hasStream = false;
        bool streamPlayQueued = false;
        bool streamStarted = false;
        bool streamStopQueued = false;
        bool streamCancelQueued = false;
        bool autoRetire = false;
        VoiceSourceKind sourceKind = VoiceSourceKind::None;
        AudioPcmClipView clip{};
        float gain = 1.0F;
        float pitch = 1.0F;
        float pan = 0.0F;
        Core::u32 mixSlot = InvalidMixSlot;
    };

    // A clip terminal that cannot be published yet because the realtime callback may
    // still be reading the caller's payload. Streams park theirs in StreamSlot;
    // clips had nowhere, so an explicit Stop published Stopped -- the caller's
    // free-the-payload signal -- while the mix block was still interpolating from
    // those frames.
    //
    // Indexed by voice index, exactly like streamSlots, so parking can never fail to
    // find a space and no path has to fall back to publishing early. That holds only
    // because a voice cannot be erased while its entry is parked: destroyVoice and
    // retirement both refuse or clear first, so the index is never recycled under a
    // live entry.
    //
    // While an entry is parked the voice reads as not playing but its frames are
    // still borrowed, so `playing` alone is not a safe predicate for "the caller may
    // free the payload" -- see the pendingClipTerminal guards on destroyVoice,
    // bindVoiceClip and clearVoiceClip.
    struct PendingClipTerminal final {
        bool pending = false;
        AudioVoiceId voice{};
        AudioCompletionKind kind = AudioCompletionKind::Stopped;
        Core::u64 commandSequence = 0;
        Core::u32 quiescingMixSlot = InvalidMixSlot;
        Core::u64 quiescingPublicationGeneration = 0;
        bool retires = false;
    };

    struct CallbackReaderGuard final {
        MixSlot* slot = nullptr;

        ~CallbackReaderGuard() noexcept
        {
            if (slot != nullptr)
            {
                (void)slot->callbackReaders.fetch_sub(1, std::memory_order_seq_cst);
            }
        }
    };

    struct RealtimeCallbackReaderGuard final {
        std::atomic<Core::u32>* admissionState = nullptr;

        ~RealtimeCallbackReaderGuard() noexcept
        {
            if (admissionState != nullptr)
            {
                (void)admissionState->fetch_sub(1, std::memory_order_seq_cst);
            }
        }
    };

    using VoicePool = Core::GenerationPool<VoiceRecord, Detail::AudioVoiceRegistryTag>;
    static constexpr Core::u32 InvalidMixSlot = (std::numeric_limits<Core::u32>::max)();
    static constexpr Core::u32 RealtimeClosedBit = Core::u32{1} << 31U;
    static constexpr Core::u32 RealtimeReaderMask = RealtimeClosedBit - 1U;
    static constexpr Core::usize MaxMixSlots = AudioMaxRealtimeVoices;

    Impl(AudioEngineConfig engineConfig, VoicePool voicePool, FixedRing<AudioCommand> commandRing,
         FixedRing<AudioCompletion> completionRing, FixedStorage<float> streamBufferStorage,
         std::thread::id ownerThread) noexcept
        : config(engineConfig),
          voices(std::move(voicePool)),
          commands(std::move(commandRing)),
          completions(std::move(completionRing)),
          streamStorage(std::move(streamBufferStorage)),
          owner(ownerThread),
          state(AudioEngineState::Disabled)
    {
        for (auto& slot : mixSlots)
        {
            slot.active.store(false, std::memory_order_relaxed);
            slot.finishedPublicationGeneration.store(0, std::memory_order_relaxed);
            slot.frames.store(nullptr, std::memory_order_relaxed);
            slot.frameCount.store(0, std::memory_order_relaxed);
            slot.channels.store(0, std::memory_order_relaxed);
            slot.sampleRate.store(0, std::memory_order_relaxed);
            slot.streaming.store(false, std::memory_order_relaxed);
            slot.streamIndex.store(0, std::memory_order_relaxed);
            slot.cursorFrame.store(0, std::memory_order_relaxed);
            slot.cursorFraction.store(0, std::memory_order_relaxed);
            slot.busGain.store(1.0F, std::memory_order_relaxed);
            slot.pitch.store(1.0F, std::memory_order_relaxed);
            slot.pan.store(0.0F, std::memory_order_relaxed);
            slot.gainControlRevision.store(0, std::memory_order_relaxed);
            slot.gainControlKind.store(static_cast<Core::u8>(VoiceGainControlKind::None),
                                       std::memory_order_relaxed);
            slot.gainControlTarget.store(1.0F, std::memory_order_relaxed);
            slot.gainControlDurationBits.store(0, std::memory_order_relaxed);
            slot.gainControlEndAction.store(static_cast<Core::u8>(AudioFadeEndAction::KeepPlaying),
                                            std::memory_order_relaxed);
            slot.appliedGainControlRevision.store(0, std::memory_order_relaxed);
            slot.currentVoiceGain.store(1.0F, std::memory_order_relaxed);
            slot.fadeStartGain.store(1.0F, std::memory_order_relaxed);
            slot.fadeTargetGain.store(1.0F, std::memory_order_relaxed);
            slot.fadeTotalFrames.store(0, std::memory_order_relaxed);
            slot.fadeElapsedFrames.store(0, std::memory_order_relaxed);
            slot.fadeEndAction.store(static_cast<Core::u8>(AudioFadeEndAction::KeepPlaying),
                                     std::memory_order_relaxed);
            slot.fadeActive.store(false, std::memory_order_relaxed);
            slot.publicationGeneration.store(1, std::memory_order_relaxed);
            slot.callbackReaders.store(0, std::memory_order_relaxed);
            slot.voice = {};
        }
        const Core::usize samplesPerVoice =
            config.streamBufferFrameCapacity * static_cast<Core::usize>(AudioPcmStreamMaxChannels);
        for (Core::usize index = 0; index < config.voiceCapacity; ++index)
        {
            StreamSlot& slot = streamSlots[index];
            slot.frames = streamStorage.storage + index * samplesPerVoice;
            slot.reservedCapacityFrames = config.streamBufferFrameCapacity;
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
        // Terminal completion debt is paid before ordinary command completions
        // can consume newly available ring capacity.
        flushPendingStreamTerminals();
        flushPendingClipTerminals();
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
                if (record->sourceKind == VoiceSourceKind::Stream)
                {
                    record->streamPlayQueued = false;
                    const StreamSlot& stream = streamSlotFor(*record);
                    if (record->streamStarted || record->streamStopQueued ||
                        record->streamCancelQueued || stream.terminalCompletionPending)
                    {
                        // Stream terminal intent is absorbing. In particular, a
                        // later Play in the same command batch cannot reactivate
                        // storage that a Stop/Cancel terminal will retire.
                        break;
                    }
                }
                if (clipTerminalParked(*record))
                {
                    // Clip terminal intent is absorbing too. Reactivating the slot here
                    // would leave a parked Stopped to be published for a voice that is
                    // playing again -- and, for a one-shot, would retire it mid-playback.
                    break;
                }
                if (record->sourceKind == VoiceSourceKind::None ||
                    (record->sourceKind == VoiceSourceKind::Clip &&
                     (!record->hasClip || record->clip.empty())) ||
                    (record->sourceKind == VoiceSourceKind::Stream &&
                     (!record->hasStream || !streamSlotFor(*record).configured)))
                {
                    record->playing = false;
                    deactivateMixSlot(*record);
                    pushCompletion(AudioCompletionKind::RejectedNoClip, command.voice, command.sequence);
                    retireTransientVoiceIfNeeded(*record);
                    break;
                }
                if (record->sourceKind == VoiceSourceKind::Stream && record->playing)
                {
                    pushCompletion(AudioCompletionKind::Started, command.voice, command.sequence);
                    break;
                }
                if (!activateMixSlot(*record, computeSfxGain()))
                {
                    record->playing = false;
                    pushCompletion(AudioCompletionKind::RejectedNoClip, command.voice, command.sequence);
                    retireTransientVoiceIfNeeded(*record);
                    break;
                }
                record->playing = true;
                if (record->sourceKind == VoiceSourceKind::Stream)
                {
                    record->streamStarted = true;
                }
                pushCompletion(AudioCompletionKind::Started, command.voice, command.sequence);
                break;
            case AudioCommandKind::Stop:
                record->playing = false;
                if (record->hasStream)
                {
                    record->streamStopQueued = false;
                    const Core::u32 mixSlot = deactivateMixSlot(*record);
                    queueStreamTerminal(
                        *record, AudioCompletionKind::Stopped, command.sequence, mixSlot);
                }
                else
                {
                    // Deferred for the same reason streams defer: Stopped is the
                    // caller's signal that the PCM frames may be freed, and the
                    // callback can still be mid-block on them. Publishing here and
                    // letting the caller drop its AssetLease was a use-after-free
                    // that only the "stop the device first" advice in docs/audio.md
                    // was hiding.
                    const Core::u32 mixSlot = deactivateMixSlot(*record);
                    queueClipTerminal(*record, AudioCompletionKind::Stopped, command.sequence,
                                      mixSlot);
                }
                break;
            case AudioCommandKind::CancelStream:
                record->playing = false;
                queueStreamTerminal(*record,
                                    AudioCompletionKind::Cancelled,
                                    command.sequence,
                                    deactivateMixSlot(*record));
                break;
            }
        }
        flushPendingStreamTerminals();
        flushPendingClipTerminals();
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
                slot.busGain.store(gain, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] static bool isValidFadeEndAction(AudioFadeEndAction action) noexcept
    {
        switch (action)
        {
        case AudioFadeEndAction::KeepPlaying:
        case AudioFadeEndAction::StopVoice:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] static Core::u64 fadeFrameCount(double durationSeconds,
                                                   Core::u32 outSampleRate) noexcept
    {
        const double exactFrames = durationSeconds * static_cast<double>(outSampleRate);
        if (!std::isfinite(exactFrames) ||
            exactFrames >= static_cast<double>((std::numeric_limits<Core::u64>::max)()))
        {
            return (std::numeric_limits<Core::u64>::max)();
        }
        if (exactFrames <= 1.0)
        {
            return 1;
        }
        return (std::max)(Core::u64{1}, static_cast<Core::u64>(std::llround(exactFrames)));
    }

    void publishVoiceGainControl(VoiceRecord& record, VoiceGainControlKind kind, float targetGain,
                                 double durationSeconds, AudioFadeEndAction endAction) noexcept
    {
        if (record.mixSlot == InvalidMixSlot || record.mixSlot >= MaxMixSlots)
        {
            return;
        }
        MixSlot& slot = mixSlots[record.mixSlot];
        (void)slot.gainControlRevision.fetch_add(1, std::memory_order_acq_rel);
        slot.gainControlKind.store(static_cast<Core::u8>(kind), std::memory_order_relaxed);
        slot.gainControlTarget.store(targetGain, std::memory_order_relaxed);
        slot.gainControlDurationBits.store(std::bit_cast<Core::u64>(durationSeconds),
                                           std::memory_order_relaxed);
        slot.gainControlEndAction.store(static_cast<Core::u8>(endAction), std::memory_order_relaxed);
        (void)slot.gainControlRevision.fetch_add(1, std::memory_order_release);
    }

    static void applyPendingVoiceGainControl(MixSlot& slot, Core::u32 outSampleRate,
                                             float& currentGain, float& fadeStartGain,
                                             float& fadeTargetGain, Core::u64& fadeTotalFrames,
                                             Core::u64& fadeElapsedFrames,
                                             AudioFadeEndAction& fadeEndAction,
                                             bool& fadeActive) noexcept
    {
        const Core::u64 revisionBefore = slot.gainControlRevision.load(std::memory_order_acquire);
        if ((revisionBefore & 1U) != 0U ||
            revisionBefore == slot.appliedGainControlRevision.load(std::memory_order_relaxed))
        {
            return;
        }

        const auto kind = static_cast<VoiceGainControlKind>(
            slot.gainControlKind.load(std::memory_order_relaxed));
        const float targetGain = slot.gainControlTarget.load(std::memory_order_relaxed);
        const double durationSeconds = std::bit_cast<double>(
            slot.gainControlDurationBits.load(std::memory_order_relaxed));
        const auto endAction = static_cast<AudioFadeEndAction>(
            slot.gainControlEndAction.load(std::memory_order_relaxed));
        const Core::u64 revisionAfter = slot.gainControlRevision.load(std::memory_order_acquire);
        if (revisionBefore != revisionAfter || (revisionAfter & 1U) != 0U)
        {
            return;
        }

        switch (kind)
        {
        case VoiceGainControlKind::SetGain:
            currentGain = targetGain;
            fadeStartGain = targetGain;
            fadeTargetGain = targetGain;
            fadeTotalFrames = 0;
            fadeElapsedFrames = 0;
            fadeEndAction = AudioFadeEndAction::KeepPlaying;
            fadeActive = false;
            break;
        case VoiceGainControlKind::StartFade:
            fadeStartGain = currentGain;
            fadeTargetGain = targetGain;
            fadeTotalFrames = fadeFrameCount(durationSeconds, outSampleRate);
            fadeElapsedFrames = 0;
            fadeEndAction = endAction;
            fadeActive = true;
            break;
        case VoiceGainControlKind::CancelFade:
            fadeStartGain = currentGain;
            fadeTargetGain = currentGain;
            fadeTotalFrames = 0;
            fadeElapsedFrames = 0;
            fadeEndAction = AudioFadeEndAction::KeepPlaying;
            fadeActive = false;
            break;
        case VoiceGainControlKind::None:
        default:
            break;
        }
        slot.appliedGainControlRevision.store(revisionAfter, std::memory_order_relaxed);
    }

    void syncVoiceGainFromMixSlot(VoiceRecord& record) noexcept
    {
        if (record.mixSlot == InvalidMixSlot || record.mixSlot >= MaxMixSlots)
        {
            return;
        }
        const float renderedGain = mixSlots[record.mixSlot].currentVoiceGain.load(std::memory_order_relaxed);
        if (std::isfinite(renderedGain))
        {
            record.gain = (std::clamp)(renderedGain, AudioVoiceMinGain, AudioVoiceMaxGain);
        }
    }

    [[nodiscard]] StreamSlot& streamSlotFor(VoiceRecord& record) noexcept
    {
        return streamSlots[record.id.index()];
    }

    [[nodiscard]] const StreamSlot& streamSlotFor(const VoiceRecord& record) const noexcept
    {
        return streamSlots[record.id.index()];
    }

    void configureStreamSlot(VoiceRecord& record, const AudioPcmStreamDesc& desc) noexcept
    {
        StreamSlot& slot = streamSlotFor(record);
        slot.capacityFrames = desc.bufferCapacityFrames;
        slot.channels = desc.channels;
        slot.sampleRate = desc.sampleRate;
        slot.readFrame.store(0, std::memory_order_relaxed);
        slot.writeFrame.store(0, std::memory_order_relaxed);
        slot.underrunFrames.store(0, std::memory_order_relaxed);
        slot.eofSignaled.store(false, std::memory_order_relaxed);
        slot.terminalCompletionPending = false;
        slot.terminalCompletion = AudioCompletionKind::Stopped;
        slot.terminalVoice = {};
        slot.terminalCommandSequence = 0;
        slot.quiescingMixSlot = InvalidMixSlot;
        slot.quiescingPublicationGeneration = 0;
        slot.configured = true;
        record.hasStream = true;
        record.streamPlayQueued = false;
        record.streamStarted = false;
        record.streamStopQueued = false;
        record.streamCancelQueued = false;
        record.sourceKind = VoiceSourceKind::Stream;
        ++streamingVoices;
    }

    void releaseStream(VoiceRecord& record) noexcept
    {
        if (!record.hasStream)
        {
            return;
        }
        StreamSlot& slot = streamSlotFor(record);
        slot.configured = false;
        slot.capacityFrames = 0;
        slot.channels = 0;
        slot.sampleRate = 0;
        slot.readFrame.store(0, std::memory_order_relaxed);
        slot.writeFrame.store(0, std::memory_order_relaxed);
        slot.eofSignaled.store(false, std::memory_order_relaxed);
        slot.terminalCompletionPending = false;
        slot.terminalVoice = {};
        slot.terminalCommandSequence = 0;
        slot.quiescingMixSlot = InvalidMixSlot;
        slot.quiescingPublicationGeneration = 0;
        record.hasStream = false;
        record.streamPlayQueued = false;
        record.streamStarted = false;
        record.streamStopQueued = false;
        record.streamCancelQueued = false;
        if (record.sourceKind == VoiceSourceKind::Stream)
        {
            record.sourceKind = VoiceSourceKind::None;
        }
        if (streamingVoices > 0)
        {
            --streamingVoices;
        }
    }

    [[nodiscard]] static Core::u64 bufferedStreamFrames(const StreamSlot& slot) noexcept
    {
        const Core::u64 readFrame = slot.readFrame.load(std::memory_order_acquire);
        const Core::u64 writeFrame = slot.writeFrame.load(std::memory_order_acquire);
        return writeFrame >= readFrame ? writeFrame - readFrame : 0;
    }

    void queueStreamTerminal(VoiceRecord& record, AudioCompletionKind kind,
                             Core::u64 commandSequence, Core::u32 quiescingMixSlot) noexcept
    {
        StreamSlot& stream = streamSlotFor(record);
        if (stream.terminalCompletionPending)
        {
            return;
        }
        record.playing = false;
        record.mixSlot = InvalidMixSlot;
        stream.terminalCompletionPending = true;
        stream.terminalCompletion = kind;
        stream.terminalVoice = record.id;
        stream.terminalCommandSequence = commandSequence;
        stream.quiescingMixSlot = quiescingMixSlot;
        stream.quiescingPublicationGeneration =
            quiescingMixSlot != InvalidMixSlot && quiescingMixSlot < config.voiceCapacity
                ? mixSlots[quiescingMixSlot].publicationGeneration.load(std::memory_order_seq_cst)
                : 0;
    }

    [[nodiscard]] PendingClipTerminal& pendingClipTerminalFor(const VoiceRecord& record) noexcept
    {
        return pendingClipTerminals[record.id.index()];
    }

    [[nodiscard]] const PendingClipTerminal& pendingClipTerminalFor(
        const VoiceRecord& record) const noexcept
    {
        return pendingClipTerminals[record.id.index()];
    }

    // True while a realtime callback may still be reading this voice's caller-owned
    // frames. `playing` is not that predicate: parking a terminal clears it, and so
    // does a rejected re-Play, while deactivateMixSlot deliberately leaves the frame
    // pointer intact for an admitted callback to finish its block.
    //
    // Reads the mix table rather than a cached flag because deactivateMixSlot already
    // records the fact: it only clears slot metadata once readers reach zero, so a
    // slot still naming this voice with a live reader is exactly the borrow. The slot
    // is inactive by then, so no new callback can enter it and the count is monotone
    // down. A recycled voice index compares unequal here, since AudioVoiceId carries
    // its generation.
    [[nodiscard]] bool clipFramesStillReadByCallback(const VoiceRecord& record) const noexcept
    {
        if (record.sourceKind == VoiceSourceKind::Stream)
        {
            return false;
        }
        for (Core::usize index = 0; index < config.voiceCapacity; ++index)
        {
            const MixSlot& slot = mixSlots[index];
            if (slot.voice == record.id &&
                slot.callbackReaders.load(std::memory_order_seq_cst) != 0)
            {
                return true;
            }
        }
        return false;
    }

    // A terminal is chosen but not yet published. Terminal intent is absorbing, so a
    // later Play must not reactivate the voice out from under it.
    [[nodiscard]] bool clipTerminalParked(const VoiceRecord& record) const noexcept
    {
        return record.sourceKind != VoiceSourceKind::Stream &&
               pendingClipTerminalFor(record).pending;
    }

    // The caller may not treat its PCM as free yet: either a callback is still
    // reading it, or a terminal completion is parked and dropping the voice now would
    // destroy the caller's only notification.
    [[nodiscard]] bool clipPayloadStillBorrowed(const VoiceRecord& record) const noexcept
    {
        return clipTerminalParked(record) || clipFramesStillReadByCallback(record);
    }

    // Parks a clip terminal until the realtime callback has provably let go of the
    // payload, mirroring queueStreamTerminal. Indexed by voice, so there is always
    // room and no caller ever loses its only free-the-payload signal.
    void queueClipTerminal(VoiceRecord& record, AudioCompletionKind kind,
                           Core::u64 commandSequence, Core::u32 quiescingMixSlot) noexcept
    {
        PendingClipTerminal& pending = pendingClipTerminalFor(record);
        if (pending.pending)
        {
            // Already queued: first terminal wins, exactly as streams behave.
            return;
        }
        record.playing = false;
        record.mixSlot = InvalidMixSlot;
        pending.pending = true;
        pending.voice = record.id;
        pending.kind = kind;
        pending.commandSequence = commandSequence;
        pending.quiescingMixSlot = quiescingMixSlot;
        pending.quiescingPublicationGeneration =
            quiescingMixSlot != InvalidMixSlot && quiescingMixSlot < config.voiceCapacity
                ? mixSlots[quiescingMixSlot].publicationGeneration.load(std::memory_order_seq_cst)
                : 0;
        pending.retires = record.autoRetire;
    }

    void flushPendingClipTerminals() noexcept
    {
        for (Core::usize index = 0; index < config.voiceCapacity; ++index)
        {
            PendingClipTerminal& pending = pendingClipTerminals[index];
            if (!pending.pending)
            {
                continue;
            }
            if (pending.quiescingMixSlot != InvalidMixSlot &&
                pending.quiescingMixSlot < config.voiceCapacity)
            {
                MixSlot& mix = mixSlots[pending.quiescingMixSlot];
                // Only wait while the slot still belongs to this terminal. Once the
                // generation moves on another voice owns the slot, and its readers
                // say nothing about whether our payload is still being read.
                if (mix.publicationGeneration.load(std::memory_order_seq_cst) ==
                        pending.quiescingPublicationGeneration &&
                    mix.callbackReaders.load(std::memory_order_seq_cst) != 0)
                {
                    continue;
                }
            }
            if (completions.full())
            {
                // Keep the token and retry next pump rather than losing the caller's
                // only free-the-payload signal.
                continue;
            }
            pushCompletion(pending.kind, pending.voice, pending.commandSequence);
            const bool retires = pending.retires;
            const AudioVoiceId voice = pending.voice;
            pending = PendingClipTerminal{};
            if (retires)
            {
                if (VoiceRecord* record = voices.tryGet(voice); record != nullptr)
                {
                    retireTransientVoiceIfNeeded(*record);
                }
            }
        }
    }

    void flushPendingStreamTerminals() noexcept
    {
        for (Core::usize index = 0; index < config.voiceCapacity; ++index)
        {
            StreamSlot& stream = streamSlots[index];
            if (!stream.configured || !stream.terminalCompletionPending)
            {
                continue;
            }
            if (stream.quiescingMixSlot != InvalidMixSlot &&
                stream.quiescingMixSlot < config.voiceCapacity)
            {
                MixSlot& mix = mixSlots[stream.quiescingMixSlot];
                if (mix.publicationGeneration.load(std::memory_order_seq_cst) ==
                    stream.quiescingPublicationGeneration)
                {
                    if (mix.callbackReaders.load(std::memory_order_seq_cst) != 0)
                    {
                        continue;
                    }
                    // A racing old callback may have raised finished after the
                    // owner queued Cancelled/Stopped. Absorb that flag into the
                    // already chosen terminal so harvest cannot emit a second one.
                    mix.active.store(false, std::memory_order_seq_cst);
                    mix.finishedPublicationGeneration.store(0, std::memory_order_seq_cst);
                    clearInactiveMixSlotMetadata(mix);
                }
                // A generation mismatch means the old publication already
                // quiesced and this slot now belongs to a newer voice. Never
                // clear metadata for that newer publication.
                stream.quiescingMixSlot = InvalidMixSlot;
                stream.quiescingPublicationGeneration = 0;
            }
            if (completions.full())
            {
                continue;
            }
            const AudioCompletion completion{
                .kind = stream.terminalCompletion,
                .voice = stream.terminalVoice,
                .commandSequence = stream.terminalCommandSequence,
            };
            if (!completions.tryPush(completion))
            {
                continue;
            }

            const AudioVoiceId voice = stream.terminalVoice;
            stream.terminalCompletionPending = false;
            if (auto* record = voices.tryGet(voice); record != nullptr)
            {
                retireTransientVoiceIfNeeded(*record);
            }
        }
    }

    [[nodiscard]] bool activateMixSlot(VoiceRecord& record, float gain) noexcept
    {
        const Core::u32 previousMixSlot = deactivateMixSlot(record);
        if (previousMixSlot != InvalidMixSlot &&
            mixSlots[previousMixSlot].callbackReaders.load(std::memory_order_seq_cst) != 0)
        {
            return false;
        }
        for (Core::u32 index = 0; index < config.voiceCapacity; ++index)
        {
            MixSlot& slot = mixSlots[index];
            if (slot.active.load(std::memory_order_seq_cst) ||
                slot.callbackReaders.load(std::memory_order_seq_cst) != 0)
            {
                continue;
            }
            // A slot holding an unharvested natural end cannot simply be taken: this
            // runs from applyCommands, which pumpCompletions calls *before*
            // harvestNaturalEnds, so overwriting the token destroyed the only record
            // that the previous voice had finished. That voice then never received
            // Stopped, stayed playing forever, could not be rebound or cleared, and
            // -- because its mixSlot still pointed here -- its gain, pitch and pan
            // setters silently retargeted whichever voice took the slot over.
            //
            // Harvested in place rather than by reordering the pump: Cancel has to be
            // able to absorb a racing natural end, which only works while
            // applyCommands still runs first.
            if (!harvestNaturalEndForSlot(index))
            {
                continue;
            }
            slot.voice = record.id;
            const bool streaming = record.sourceKind == VoiceSourceKind::Stream;
            slot.streaming.store(streaming, std::memory_order_relaxed);
            if (streaming)
            {
                StreamSlot& stream = streamSlotFor(record);
                slot.streamIndex.store(record.id.index(), std::memory_order_relaxed);
                slot.frames.store(nullptr, std::memory_order_relaxed);
                slot.frameCount.store(0, std::memory_order_relaxed);
                slot.channels.store(stream.channels, std::memory_order_relaxed);
                slot.sampleRate.store(stream.sampleRate, std::memory_order_relaxed);
                slot.cursorFrame.store(stream.readFrame.load(std::memory_order_acquire),
                                       std::memory_order_relaxed);
            }
            else
            {
                slot.streamIndex.store(0, std::memory_order_relaxed);
                slot.frames.store(record.clip.frames, std::memory_order_relaxed);
                slot.frameCount.store(record.clip.frameCount, std::memory_order_relaxed);
                slot.channels.store(record.clip.channels, std::memory_order_relaxed);
                slot.sampleRate.store(record.clip.sampleRate, std::memory_order_relaxed);
                slot.cursorFrame.store(0, std::memory_order_relaxed);
            }
            slot.cursorFraction.store(0, std::memory_order_relaxed);
            slot.busGain.store(gain, std::memory_order_relaxed);
            slot.pitch.store(record.pitch, std::memory_order_relaxed);
            slot.pan.store(record.pan, std::memory_order_relaxed);
            slot.gainControlRevision.store(0, std::memory_order_relaxed);
            slot.gainControlKind.store(static_cast<Core::u8>(VoiceGainControlKind::None),
                                       std::memory_order_relaxed);
            slot.gainControlTarget.store(record.gain, std::memory_order_relaxed);
            slot.gainControlDurationBits.store(0, std::memory_order_relaxed);
            slot.gainControlEndAction.store(static_cast<Core::u8>(AudioFadeEndAction::KeepPlaying),
                                            std::memory_order_relaxed);
            slot.appliedGainControlRevision.store(0, std::memory_order_relaxed);
            slot.currentVoiceGain.store(record.gain, std::memory_order_relaxed);
            slot.fadeStartGain.store(record.gain, std::memory_order_relaxed);
            slot.fadeTargetGain.store(record.gain, std::memory_order_relaxed);
            slot.fadeTotalFrames.store(0, std::memory_order_relaxed);
            slot.fadeElapsedFrames.store(0, std::memory_order_relaxed);
            slot.fadeEndAction.store(static_cast<Core::u8>(AudioFadeEndAction::KeepPlaying),
                                     std::memory_order_relaxed);
            slot.fadeActive.store(false, std::memory_order_relaxed);
            slot.finishedPublicationGeneration.store(0, std::memory_order_seq_cst);
            (void)slot.publicationGeneration.fetch_add(1, std::memory_order_seq_cst);
            slot.active.store(true, std::memory_order_seq_cst);
            record.mixSlot = index;
            return true;
        }
        return false;
    }

    static void clearInactiveMixSlotMetadata(MixSlot& slot) noexcept
    {
        slot.frames.store(nullptr, std::memory_order_relaxed);
        slot.streaming.store(false, std::memory_order_relaxed);
        slot.streamIndex.store(0, std::memory_order_relaxed);
        slot.cursorFrame.store(0, std::memory_order_relaxed);
        slot.cursorFraction.store(0, std::memory_order_relaxed);
        slot.fadeActive.store(false, std::memory_order_relaxed);
        slot.gainControlRevision.store(0, std::memory_order_relaxed);
        slot.appliedGainControlRevision.store(0, std::memory_order_relaxed);
        slot.voice = {};
    }

    static void finishMixSlotFromCallback(MixSlot& slot,
                                          Core::u64 publicationGeneration) noexcept
    {
        // Publication epochs are owner-only. The reader guard keeps source
        // storage alive until the owner observes quiescence.
        slot.active.store(false, std::memory_order_seq_cst);
        slot.finishedPublicationGeneration.store(publicationGeneration,
                                                 std::memory_order_seq_cst);
    }

    Core::u32 deactivateMixSlot(VoiceRecord& record) noexcept
    {
        if (record.mixSlot == InvalidMixSlot || record.mixSlot >= MaxMixSlots)
        {
            record.mixSlot = InvalidMixSlot;
            return InvalidMixSlot;
        }
        const Core::u32 mixSlot = record.mixSlot;
        MixSlot& slot = mixSlots[mixSlot];
        syncVoiceGainFromMixSlot(record);
        slot.active.store(false, std::memory_order_seq_cst);
        (void)slot.publicationGeneration.fetch_add(1, std::memory_order_seq_cst);
        slot.finishedPublicationGeneration.store(0, std::memory_order_seq_cst);
        record.mixSlot = InvalidMixSlot;
        if (slot.callbackReaders.load(std::memory_order_seq_cst) == 0)
        {
            clearInactiveMixSlotMetadata(slot);
        }
        return mixSlot;
    }

    void releaseBoundClip(VoiceRecord& record) noexcept
    {
        if (!record.hasClip)
        {
            return;
        }
        record.hasClip = false;
        record.clip = {};
        if (record.sourceKind == VoiceSourceKind::Clip)
        {
            record.sourceKind = VoiceSourceKind::None;
        }
        if (boundClipVoices > 0)
        {
            --boundClipVoices;
        }
    }

    void retireTransientVoiceIfNeeded(VoiceRecord& record) noexcept
    {
        if (!record.autoRetire)
        {
            return;
        }
        const AudioVoiceId voice = record.id;
        releaseBoundClip(record);
        releaseStream(record);
        (void)voices.erase(voice);
    }

    // Converts one slot's natural-end flag into a terminal completion. Returns true
    // when the slot is left free for reuse -- either it held no terminal, or the
    // terminal was consumed here. False means a callback is still reading it.
    //
    // Split out of harvestNaturalEnds so activateMixSlot can drain a slot before
    // taking it: applyCommands runs before the pump's harvest, so a Play arriving in
    // the same pump used to overwrite the token and lose the previous voice's Stopped.
    [[nodiscard]] bool harvestNaturalEndForSlot(Core::u32 index) noexcept
    {
        MixSlot& slot = mixSlots[index];
        Core::u64 finishedPublicationGeneration =
            slot.finishedPublicationGeneration.load(std::memory_order_seq_cst);
        if (finishedPublicationGeneration == 0)
        {
            return true;
        }
        if (slot.callbackReaders.load(std::memory_order_seq_cst) != 0)
        {
            return false;
        }
        const Core::u64 currentPublicationGeneration =
            slot.publicationGeneration.load(std::memory_order_seq_cst);
        if (finishedPublicationGeneration != currentPublicationGeneration)
        {
            // The callback belonged to a publication that the owner already stopped.
            // Clear only that stale terminal token; never inspect or clear metadata
            // owned by a newer publication.
            (void)slot.finishedPublicationGeneration.compare_exchange_strong(
                finishedPublicationGeneration,
                0,
                std::memory_order_seq_cst,
                std::memory_order_seq_cst);
            return true;
        }
        if (!slot.finishedPublicationGeneration.compare_exchange_strong(
                finishedPublicationGeneration,
                0,
                std::memory_order_seq_cst,
                std::memory_order_seq_cst))
        {
            return false;
        }
        slot.active.store(false, std::memory_order_seq_cst);
        (void)slot.publicationGeneration.fetch_add(1, std::memory_order_seq_cst);
        const AudioVoiceId voice = slot.voice;
        if (!voice.hasValue())
        {
            clearInactiveMixSlotMetadata(slot);
            return true;
        }
        if (auto* record = voices.tryGet(voice); record != nullptr && record->mixSlot == index)
        {
            record->gain = (std::clamp)(slot.currentVoiceGain.load(std::memory_order_relaxed),
                                        AudioVoiceMinGain,
                                        AudioVoiceMaxGain);
            record->playing = false;
            record->mixSlot = InvalidMixSlot;
            if (record->hasStream)
            {
                queueStreamTerminal(*record, AudioCompletionKind::Stopped, 0, index);
            }
            else
            {
                retireTransientVoiceIfNeeded(*record);
                pushCompletion(AudioCompletionKind::Stopped, voice, 0);
            }
        }
        clearInactiveMixSlotMetadata(slot);
        return true;
    }

    void harvestNaturalEnds() noexcept
    {
        for (Core::u32 index = 0; index < config.voiceCapacity; ++index)
        {
            (void)harvestNaturalEndForSlot(index);
        }
        flushPendingStreamTerminals();
        flushPendingClipTerminals();
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
        Core::u32 admission = realtimeAdmissionState.load(std::memory_order_seq_cst);
        for (;;)
        {
            if ((admission & RealtimeClosedBit) != 0 ||
                (admission & RealtimeReaderMask) == RealtimeReaderMask)
            {
                return;
            }
            if (realtimeAdmissionState.compare_exchange_weak(admission,
                                                             admission + 1U,
                                                             std::memory_order_seq_cst,
                                                             std::memory_order_seq_cst))
            {
                break;
            }
        }
        RealtimeCallbackReaderGuard realtimeReaderGuard{
            .admissionState = &realtimeAdmissionState,
        };
        if ((admission & RealtimeReaderMask) != 0 ||
            (realtimeAdmissionState.load(std::memory_order_seq_cst) & RealtimeClosedBit) != 0)
        {
            // Exactly one realtime consumer owns the mixer. A second overlapping
            // callback observes the already-zeroed output and returns immediately.
            return;
        }

        for (auto& slot : mixSlots)
        {
            const Core::u64 publicationGeneration =
                slot.publicationGeneration.load(std::memory_order_seq_cst);
            if (!slot.active.load(std::memory_order_seq_cst))
            {
                continue;
            }
            (void)slot.callbackReaders.fetch_add(1, std::memory_order_seq_cst);
            CallbackReaderGuard readerGuard{.slot = &slot};
            if ((realtimeAdmissionState.load(std::memory_order_seq_cst) & RealtimeClosedBit) != 0 ||
                !slot.active.load(std::memory_order_seq_cst) ||
                slot.publicationGeneration.load(std::memory_order_seq_cst) != publicationGeneration)
            {
                continue;
            }
            const bool streaming = slot.streaming.load(std::memory_order_relaxed);
            const float* clipFrames = slot.frames.load(std::memory_order_relaxed);
            const Core::u64 clipFrameCount = slot.frameCount.load(std::memory_order_relaxed);
            const Core::u32 channels = slot.channels.load(std::memory_order_relaxed);
            const Core::u32 sampleRate = slot.sampleRate.load(std::memory_order_relaxed);
            StreamSlot* stream = nullptr;
            if (streaming)
            {
                const Core::u32 streamIndex = slot.streamIndex.load(std::memory_order_relaxed);
                if (streamIndex >= config.voiceCapacity || !streamSlots[streamIndex].configured)
                {
                    finishMixSlotFromCallback(slot, publicationGeneration);
                    continue;
                }
                stream = &streamSlots[streamIndex];
            }
            else if (clipFrames == nullptr || clipFrameCount == 0)
            {
                continue;
            }
            if (channels == 0 || sampleRate == 0)
            {
                continue;
            }
            Core::u64 cursorFrame = slot.cursorFrame.load(std::memory_order_relaxed);
            Core::u32 cursorFraction = slot.cursorFraction.load(std::memory_order_relaxed);
            if (!streaming && cursorFrame >= clipFrameCount)
            {
                finishMixSlotFromCallback(slot, publicationGeneration);
                continue;
            }
            const float busGain = slot.busGain.load(std::memory_order_relaxed);
            const float pitch = slot.pitch.load(std::memory_order_relaxed);
            const float pan = slot.pan.load(std::memory_order_relaxed);
            float currentVoiceGain = slot.currentVoiceGain.load(std::memory_order_relaxed);
            float fadeStartGain = slot.fadeStartGain.load(std::memory_order_relaxed);
            float fadeTargetGain = slot.fadeTargetGain.load(std::memory_order_relaxed);
            Core::u64 fadeTotalFrames = slot.fadeTotalFrames.load(std::memory_order_relaxed);
            Core::u64 fadeElapsedFrames = slot.fadeElapsedFrames.load(std::memory_order_relaxed);
            auto fadeEndAction = static_cast<AudioFadeEndAction>(
                slot.fadeEndAction.load(std::memory_order_relaxed));
            bool fadeActive = slot.fadeActive.load(std::memory_order_relaxed);
            applyPendingVoiceGainControl(slot,
                                         outSampleRate,
                                         currentVoiceGain,
                                         fadeStartGain,
                                         fadeTargetGain,
                                         fadeTotalFrames,
                                         fadeElapsedFrames,
                                         fadeEndAction,
                                         fadeActive);

            constexpr double FractionScale = 4294967296.0;
            const double sourceStep =
                static_cast<double>(sampleRate) / static_cast<double>(outSampleRate) * pitch;
            const double stepQ32Double = sourceStep * FractionScale;
            const Core::u64 stepQ32 = (std::max)(
                Core::u64{1},
                stepQ32Double >= static_cast<double>((std::numeric_limits<Core::u64>::max)())
                    ? (std::numeric_limits<Core::u64>::max)()
                    : static_cast<Core::u64>(std::llround(stepQ32Double)));
            const Core::u64 stepWhole = stepQ32 >> 32U;
            const Core::u32 stepFraction = static_cast<Core::u32>(stepQ32 & 0xFFFF'FFFFULL);

            bool stoppedByFade = false;
            for (Core::u32 f = 0; f < outFrames; ++f)
            {
                if (fadeActive)
                {
                    ++fadeElapsedFrames;
                    const double progress = (std::min)(
                        1.0,
                        static_cast<double>(fadeElapsedFrames) /
                            static_cast<double>((std::max)(Core::u64{1}, fadeTotalFrames)));
                    currentVoiceGain = static_cast<float>(
                        static_cast<double>(fadeStartGain) +
                        (static_cast<double>(fadeTargetGain) - static_cast<double>(fadeStartGain)) * progress);
                    if (fadeElapsedFrames >= fadeTotalFrames)
                    {
                        currentVoiceGain = fadeTargetGain;
                        fadeActive = false;
                        stoppedByFade = fadeEndAction == AudioFadeEndAction::StopVoice;
                    }
                }

                Core::u64 nextFrame = cursorFrame;
                const float* sourceFrames = clipFrames;
                Core::u64 base = 0;
                Core::u64 nextBase = 0;
                if (streaming)
                {
                    const bool eofSignaled = stream->eofSignaled.load(std::memory_order_acquire);
                    const Core::u64 writeFrame = stream->writeFrame.load(std::memory_order_acquire);
                    const Core::u64 consumedFrame = (std::min)(cursorFrame, writeFrame);
                    stream->readFrame.store(consumedFrame, std::memory_order_release);
                    if (cursorFrame >= writeFrame)
                    {
                        if (eofSignaled || stoppedByFade)
                        {
                            finishMixSlotFromCallback(slot, publicationGeneration);
                            break;
                        }
                        (void)stream->underrunFrames.fetch_add(1, std::memory_order_relaxed);
                        (void)streamUnderrunFrames.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    nextFrame = cursorFrame + 1U < writeFrame ? cursorFrame + 1U : cursorFrame;
                    if (cursorFraction != 0 && nextFrame == cursorFrame && !eofSignaled)
                    {
                        if (stoppedByFade)
                        {
                            finishMixSlotFromCallback(slot, publicationGeneration);
                            break;
                        }
                        (void)stream->underrunFrames.fetch_add(1, std::memory_order_relaxed);
                        (void)streamUnderrunFrames.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    sourceFrames = stream->frames;
                    const Core::u64 currentRingFrame =
                        cursorFrame % static_cast<Core::u64>(stream->capacityFrames);
                    const Core::u64 nextRingFrame =
                        nextFrame % static_cast<Core::u64>(stream->capacityFrames);
                    base = currentRingFrame * channels;
                    nextBase = nextRingFrame * channels;
                }
                else
                {
                    if (cursorFrame >= clipFrameCount)
                    {
                        finishMixSlotFromCallback(slot, publicationGeneration);
                        break;
                    }
                    nextFrame = cursorFrame + 1U < clipFrameCount ? cursorFrame + 1U : cursorFrame;
                    base = cursorFrame * channels;
                    nextBase = nextFrame * channels;
                }

                const double fraction = static_cast<double>(cursorFraction) / FractionScale;
                const auto interpolate = [sourceFrames, fraction](Core::u64 first,
                                                                  Core::u64 second) noexcept {
                    return static_cast<float>(
                        static_cast<double>(sourceFrames[first]) +
                        (static_cast<double>(sourceFrames[second]) -
                         static_cast<double>(sourceFrames[first])) *
                            fraction);
                };
                const float effectiveGain = busGain * currentVoiceGain;
                if (outChannels == 1)
                {
                    float sample = interpolate(base, nextBase);
                    if (channels >= 2)
                    {
                        sample = 0.5F *
                                 (interpolate(base, nextBase) +
                                  interpolate(base + 1U, nextBase + 1U));
                    }
                    interleavedOut[f] += sample * effectiveGain;
                }
                else
                {
                    const float left = interpolate(base, nextBase);
                    const float right = channels >= 2
                                            ? interpolate(base + 1U, nextBase + 1U)
                                            : left;
                    const float leftPanGain = pan <= 0.0F ? 1.0F : 1.0F - pan;
                    const float rightPanGain = pan >= 0.0F ? 1.0F : 1.0F + pan;
                    interleavedOut[static_cast<Core::usize>(f) * 2U] +=
                        left * effectiveGain * leftPanGain;
                    interleavedOut[static_cast<Core::usize>(f) * 2U + 1U] +=
                        right * effectiveGain * rightPanGain;
                }

                const Core::u64 fractionSum = static_cast<Core::u64>(cursorFraction) + stepFraction;
                const Core::u64 wholeAdvance = stepWhole + (fractionSum >> 32U);
                cursorFraction = static_cast<Core::u32>(fractionSum & 0xFFFF'FFFFULL);
                if (wholeAdvance > (std::numeric_limits<Core::u64>::max)() - cursorFrame)
                {
                    cursorFrame = streaming ? (std::numeric_limits<Core::u64>::max)() : clipFrameCount;
                }
                else
                {
                    cursorFrame += wholeAdvance;
                }

                if (streaming)
                {
                    const bool eofSignaled = stream->eofSignaled.load(std::memory_order_acquire);
                    const Core::u64 writeFrame = stream->writeFrame.load(std::memory_order_acquire);
                    stream->readFrame.store((std::min)(cursorFrame, writeFrame),
                                            std::memory_order_release);
                    if (eofSignaled && cursorFrame >= writeFrame)
                    {
                        finishMixSlotFromCallback(slot, publicationGeneration);
                        break;
                    }
                }

                if (stoppedByFade)
                {
                    finishMixSlotFromCallback(slot, publicationGeneration);
                    break;
                }
            }
            slot.cursorFrame.store(cursorFrame, std::memory_order_relaxed);
            slot.cursorFraction.store(cursorFraction, std::memory_order_relaxed);
            slot.currentVoiceGain.store(currentVoiceGain, std::memory_order_relaxed);
            slot.fadeStartGain.store(fadeStartGain, std::memory_order_relaxed);
            slot.fadeTargetGain.store(fadeTargetGain, std::memory_order_relaxed);
            slot.fadeTotalFrames.store(fadeTotalFrames, std::memory_order_relaxed);
            slot.fadeElapsedFrames.store(fadeElapsedFrames, std::memory_order_relaxed);
            slot.fadeEndAction.store(static_cast<Core::u8>(fadeEndAction), std::memory_order_relaxed);
            slot.fadeActive.store(fadeActive, std::memory_order_relaxed);
            if (!streaming && cursorFrame >= clipFrameCount && !stoppedByFade)
            {
                finishMixSlotFromCallback(slot, publicationGeneration);
            }
        }
        (void)mixFramesRendered.fetch_add(outFrames, std::memory_order_relaxed);
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
            case AudioCompletionKind::Cancelled:
                ++completedCancelled;
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
        if (clip.frameCount >
            (std::numeric_limits<Core::usize>::max)() / static_cast<Core::usize>(clip.channels))
        {
            return fail(AudioErrorCode::InvalidConfiguration, "AudioPcmClipView sample count overflowed");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status validateStreamDesc(const AudioPcmStreamDesc& desc) const noexcept
    {
        if (desc.channels == 0 || desc.channels > AudioPcmStreamMaxChannels)
        {
            return fail(AudioErrorCode::InvalidConfiguration,
                        "AudioPcmStreamDesc channels must be in [1, 2]");
        }
        if (desc.sampleRate < 1000 || desc.sampleRate > 192000)
        {
            return fail(AudioErrorCode::InvalidConfiguration,
                        "AudioPcmStreamDesc sampleRate out of range");
        }
        if (desc.bufferCapacityFrames < AudioPcmStreamMinBufferFrames ||
            desc.bufferCapacityFrames > config.streamBufferFrameCapacity)
        {
            return fail(AudioErrorCode::InvalidConfiguration,
                        "AudioPcmStreamDesc buffer capacity is invalid");
        }
        return Core::success();
    }

    [[nodiscard]] Core::usize countBoundClips() const noexcept
    {
        // GenerationPool has no full scan iterator; track on bind/clear instead.
        return boundClipVoices;
    }

    [[nodiscard]] Core::usize countBufferedStreamFrames() const noexcept
    {
        Core::usize total = 0;
        for (Core::usize index = 0; index < config.voiceCapacity; ++index)
        {
            const StreamSlot& slot = streamSlots[index];
            if (!slot.configured)
            {
                continue;
            }
            const Core::u64 buffered = bufferedStreamFrames(slot);
            const Core::usize remaining = (std::numeric_limits<Core::usize>::max)() - total;
            total += static_cast<Core::usize>((std::min)(buffered, static_cast<Core::u64>(remaining)));
        }
        return total;
    }

    AudioEngineConfig config{};
    VoicePool voices;
    FixedRing<AudioCommand> commands{};
    FixedRing<AudioCompletion> completions{};
    FixedStorage<float> streamStorage{};
    std::array<AudioBusState, AudioBusCount> buses{};
    std::array<MixSlot, MaxMixSlots> mixSlots{};
    std::array<StreamSlot, MaxMixSlots> streamSlots{};
    // One entry per possible voice, so a queued clip terminal always has somewhere
    // to wait for the realtime callback to release the caller's payload.
    std::array<PendingClipTerminal, MaxMixSlots> pendingClipTerminals{};
    std::thread::id owner{};
    AudioEngineState state = AudioEngineState::Uninitialized;
    bool closed = false;
    // Closed and reader count share one SC word, so shutdown cannot miss a
    // callback between a separate gate check and reader-count increment.
    std::atomic<Core::u32> realtimeAdmissionState{0};
    Core::u64 nextCommandSequence = 0;
    Core::usize rejectedCommands = 0;
    Core::usize completedStarted = 0;
    Core::usize completedStopped = 0;
    Core::usize completedRejectedStale = 0;
    Core::usize completedRejectedNoClip = 0;
    Core::usize completedCancelled = 0;
    Core::usize boundClipVoices = 0;
    Core::usize streamingVoices = 0;
    std::atomic<Core::u64> mixFramesRendered{0};
    std::atomic<Core::u64> streamUnderrunFrames{0};
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

template <typename T>
[[nodiscard]] Core::Result<FixedStorage<T>> allocateStorage(
    Core::usize count,
    std::pmr::memory_resource& resource) noexcept
{
    FixedStorage<T> storage{};
    try
    {
        void* raw = resource.allocate(sizeof(T) * count, alignof(T));
        storage.storage = static_cast<T*>(raw);
        storage.count = count;
        storage.resource = &resource;
        std::uninitialized_value_construct_n(storage.storage, count);
        return storage;
    }
    catch (const std::bad_alloc&)
    {
        storage.destroy();
        return Core::failure(AudioErrorCode::ConstructionFailed,
                             "AudioEngine stream storage allocation failed");
    }
    catch (const std::exception& exception)
    {
        storage.destroy();
        return Core::failure(AudioErrorCode::ConstructionFailed,
                             std::string_view(exception.what()));
    }
    catch (...)
    {
        storage.destroy();
        return Core::failure(AudioErrorCode::ConstructionFailed,
                             "AudioEngine stream storage allocation failed");
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

    const Core::usize streamSampleCapacity =
        config.voiceCapacity * config.streamBufferFrameCapacity *
        static_cast<Core::usize>(AudioPcmStreamMaxChannels);
    auto streamStorage = allocateStorage<float>(streamSampleCapacity, resource);
    if (!streamStorage)
    {
        commandRing->destroy();
        completionRing->destroy();
        return Core::failure(streamStorage.error());
    }

    try
    {
        auto* impl = new Impl(config,
                              std::move(*voices),
                              std::move(*commandRing),
                              std::move(*completionRing),
                              std::move(*streamStorage),
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
        if (streamStorage)
        {
            streamStorage->destroy();
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
        if (streamStorage)
        {
            streamStorage->destroy();
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
        if (streamStorage)
        {
            streamStorage->destroy();
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
        .completedCancelled = m_impl->completedCancelled,
        .boundClipVoices = m_impl->boundClipVoices,
        .streamingVoices = m_impl->streamingVoices,
        .streamBufferedFrames = m_impl->countBufferedStreamFrames(),
        .activeMixVoices = m_impl->countActiveMixSlots(),
        .mixFramesRendered = m_impl->mixFramesRendered.load(std::memory_order_relaxed),
        .streamUnderrunFrames = m_impl->streamUnderrunFrames.load(std::memory_order_relaxed),
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
        record->hasStream = false;
        record->streamPlayQueued = false;
        record->streamStarted = false;
        record->streamStopQueued = false;
        record->streamCancelQueued = false;
        record->autoRetire = false;
        record->sourceKind = VoiceSourceKind::None;
        record->clip = {};
        record->gain = 1.0F;
        record->pitch = 1.0F;
        record->pan = 0.0F;
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
        if (record->hasStream)
        {
            return fail(AudioErrorCode::InvalidConfiguration,
                        "Streaming voices must finish by EOF, cancelPcmStream, or enqueueStop");
        }
        // Erasing a playing clip voice used to publish nothing at all: the record
        // vanished while deactivateMixSlot deliberately left slot.frames intact for
        // the in-flight callback, so the caller was never told when its PCM stopped
        // being read. Fail closed and make it go through Stop, which parks a terminal
        // and delivers exactly that signal. Refusing here is also what keeps the
        // pending table indexable by voice -- an erased index must never be recycled
        // while an entry for it is still parked.
        if (record->playing || m_impl->clipPayloadStillBorrowed(*record))
        {
            return fail(AudioErrorCode::InvalidConfiguration,
                        "Clip voices must finish by enqueueStop so the caller learns when the "
                        "realtime callback released the PCM frames");
        }
        m_impl->releaseBoundClip(*record);
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
    // A parked terminal means Stop already cleared `playing` but the callback may
    // still be mid-block on the old frames. Rebinding would tell the caller the old
    // clip is free while it is still being read.
    if (m_impl->clipPayloadStillBorrowed(*record))
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "Cannot rebind clip until the pending Stop completion is pumped");
    }
    if (record->hasStream)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "Cannot bind a PCM clip to a streaming voice");
    }
    const bool hadClip = record->hasClip;
    record->clip = clip;
    record->hasClip = true;
    record->sourceKind = VoiceSourceKind::Clip;
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
    // Same window as bindVoiceClip: clearing is the caller's cue that it owns the
    // frames again, and a parked terminal means the callback has not let go yet.
    if (m_impl->clipPayloadStillBorrowed(*record))
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "Cannot clear clip until the pending Stop completion is pumped");
    }
    if (record->hasClip)
    {
        m_impl->releaseBoundClip(*record);
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

Core::Status AudioEngine::setVoiceGain(AudioVoiceId voice, float gain) noexcept
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
    if (!std::isfinite(gain) || gain < AudioVoiceMinGain || gain > AudioVoiceMaxGain)
    {
        return fail(AudioErrorCode::InvalidConfiguration, "Audio voice gain must be finite in [0, 1]");
    }
    auto* record = m_impl->voices.tryGet(voice);
    if (record == nullptr)
    {
        return fail(AudioErrorCode::StaleVoice, "AudioVoiceId is not live");
    }

    record->gain = gain;
    if (record->playing)
    {
        m_impl->publishVoiceGainControl(
            *record, VoiceGainControlKind::SetGain, gain, 0.0, AudioFadeEndAction::KeepPlaying);
    }
    return Core::success();
}

Core::Status AudioEngine::setVoicePitch(AudioVoiceId voice, float pitch) noexcept
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
    if (!std::isfinite(pitch) || pitch < AudioVoiceMinPitch || pitch > AudioVoiceMaxPitch)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "Audio voice pitch must be finite in [0.25, 4]");
    }
    auto* record = m_impl->voices.tryGet(voice);
    if (record == nullptr)
    {
        return fail(AudioErrorCode::StaleVoice, "AudioVoiceId is not live");
    }

    record->pitch = pitch;
    if (record->mixSlot != Impl::InvalidMixSlot && record->mixSlot < Impl::MaxMixSlots)
    {
        m_impl->mixSlots[record->mixSlot].pitch.store(pitch, std::memory_order_relaxed);
    }
    return Core::success();
}

Core::Status AudioEngine::setVoicePan(AudioVoiceId voice, float pan) noexcept
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
    if (!std::isfinite(pan) || pan < AudioVoiceMinPan || pan > AudioVoiceMaxPan)
    {
        return fail(AudioErrorCode::InvalidConfiguration, "Audio voice pan must be finite in [-1, 1]");
    }
    auto* record = m_impl->voices.tryGet(voice);
    if (record == nullptr)
    {
        return fail(AudioErrorCode::StaleVoice, "AudioVoiceId is not live");
    }

    record->pan = pan;
    if (record->mixSlot != Impl::InvalidMixSlot && record->mixSlot < Impl::MaxMixSlots)
    {
        m_impl->mixSlots[record->mixSlot].pan.store(pan, std::memory_order_relaxed);
    }
    return Core::success();
}

Core::Status AudioEngine::startVoiceFade(AudioVoiceId voice, AudioVoiceFadeDesc fade) noexcept
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
    if (!std::isfinite(fade.targetGain) || fade.targetGain < AudioVoiceMinGain ||
        fade.targetGain > AudioVoiceMaxGain)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "Audio fade target gain must be finite in [0, 1]");
    }
    const double durationSeconds = fade.duration.count();
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0)
    {
        return fail(AudioErrorCode::InvalidConfiguration, "Audio fade duration must be finite and positive");
    }
    if (!Impl::isValidFadeEndAction(fade.endAction))
    {
        return fail(AudioErrorCode::InvalidConfiguration, "Audio fade end action is invalid");
    }
    auto* record = m_impl->voices.tryGet(voice);
    if (record == nullptr)
    {
        return fail(AudioErrorCode::StaleVoice, "AudioVoiceId is not live");
    }
    if (!record->playing || record->mixSlot == Impl::InvalidMixSlot ||
        record->mixSlot >= Impl::MaxMixSlots ||
        !m_impl->mixSlots[record->mixSlot].active.load(std::memory_order_acquire))
    {
        return fail(AudioErrorCode::InvalidConfiguration, "Audio voice must be actively playing to start a fade");
    }

    m_impl->publishVoiceGainControl(
        *record, VoiceGainControlKind::StartFade, fade.targetGain, durationSeconds, fade.endAction);
    return Core::success();
}

Core::Status AudioEngine::cancelVoiceFade(AudioVoiceId voice) noexcept
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
    if (record->playing && record->mixSlot != Impl::InvalidMixSlot &&
        record->mixSlot < Impl::MaxMixSlots)
    {
        m_impl->publishVoiceGainControl(
            *record, VoiceGainControlKind::CancelFade, record->gain, 0.0, AudioFadeEndAction::KeepPlaying);
    }
    return Core::success();
}

Core::Result<AudioVoicePlaybackState> AudioEngine::voicePlaybackState(AudioVoiceId voice) const noexcept
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

    AudioVoicePlaybackState playbackState{
        .gain = record->gain,
        .pitch = record->pitch,
        .pan = record->pan,
        .playing = record->playing,
        .fadeActive = false,
    };
    if (record->mixSlot != Impl::InvalidMixSlot && record->mixSlot < Impl::MaxMixSlots)
    {
        const auto& slot = m_impl->mixSlots[record->mixSlot];
        playbackState.gain = slot.currentVoiceGain.load(std::memory_order_relaxed);
        playbackState.fadeActive = slot.fadeActive.load(std::memory_order_relaxed);
    }
    return playbackState;
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
    if (record->hasStream)
    {
        const auto& stream = m_impl->streamSlotFor(*record);
        if (record->streamPlayQueued || record->streamStarted || record->streamStopQueued ||
            record->streamCancelQueued || stream.terminalCompletionPending ||
            stream.eofSignaled.load(std::memory_order_acquire))
        {
            return fail(AudioErrorCode::InvalidConfiguration,
                        "PCM stream play is already queued, started, or terminal");
        }
    }
    Core::Status status = m_impl->enqueueCommand(AudioCommandKind::Play, voice);
    if (status && record->hasStream)
    {
        record->streamPlayQueued = true;
    }
    return status;
}

Core::Status AudioEngine::enqueueStop(AudioVoiceId voice) noexcept
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
    if (record->hasStream)
    {
        auto& stream = m_impl->streamSlotFor(*record);
        if (record->streamStopQueued)
        {
            return Core::success();
        }
        if (record->streamCancelQueued || stream.terminalCompletionPending)
        {
            return fail(AudioErrorCode::InvalidConfiguration,
                        "PCM stream terminal completion is already pending");
        }
    }
    Core::Status status = m_impl->enqueueCommand(AudioCommandKind::Stop, voice);
    if (status && record->hasStream)
    {
        record->streamStopQueued = true;
    }
    return status;
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
    if (auto* record = m_impl->voices.tryGet(*voice); record != nullptr)
    {
        record->autoRetire = true;
    }
    return *voice;
}

Core::Result<AudioVoiceId> AudioEngine::playPcmStream(AudioPcmStreamDesc desc) noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (Core::Status status = m_impl->requireOpenOwner(); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = m_impl->validateStreamDesc(desc); !status)
    {
        return Core::failure(status.error());
    }

    auto voice = createVoice();
    if (!voice)
    {
        return Core::failure(voice.error());
    }
    auto* record = m_impl->voices.tryGet(*voice);
    if (record == nullptr)
    {
        return Core::failure(AudioErrorCode::ConstructionFailed,
                             "AudioEngine failed to publish the streaming voice");
    }

    m_impl->configureStreamSlot(*record, desc);
    record->autoRetire = true;
    if (Core::Status status = enqueuePlay(*voice); !status)
    {
        m_impl->releaseStream(*record);
        (void)m_impl->voices.erase(*voice);
        return Core::failure(status.error());
    }
    return *voice;
}

Core::Status AudioEngine::submitPcmStreamFrames(
    AudioVoiceId voice,
    AudioPcmStreamChunkView chunk) noexcept
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
    if (!record->hasStream || record->sourceKind != VoiceSourceKind::Stream)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "AudioVoiceId is not a PCM streaming voice");
    }

    auto& stream = m_impl->streamSlotFor(*record);
    if (!stream.configured || stream.terminalCompletionPending ||
        record->streamStopQueued || record->streamCancelQueued)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "PCM stream is stopping, cancelling, or terminal");
    }
    if (stream.eofSignaled.load(std::memory_order_acquire))
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "PCM stream does not accept frames after EOF");
    }
    if (chunk.empty())
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "AudioPcmStreamChunkView is empty");
    }
    if (chunk.frameCount > static_cast<Core::u64>(stream.capacityFrames) ||
        chunk.frameCount >
            static_cast<Core::u64>((std::numeric_limits<Core::usize>::max)() /
                                   static_cast<Core::usize>(stream.channels)))
    {
        return fail(AudioErrorCode::CapacityExceeded,
                    "PCM stream chunk exceeds the fixed stream capacity");
    }

    const Core::u64 readFrame = stream.readFrame.load(std::memory_order_acquire);
    const Core::u64 writeFrame = stream.writeFrame.load(std::memory_order_relaxed);
    if (writeFrame < readFrame || writeFrame - readFrame > stream.capacityFrames)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "PCM stream ring counters are inconsistent");
    }
    const Core::u64 freeFrames =
        static_cast<Core::u64>(stream.capacityFrames) - (writeFrame - readFrame);
    if (chunk.frameCount > freeFrames)
    {
        return fail(AudioErrorCode::CapacityExceeded,
                    "PCM stream ring does not have enough free frames");
    }
    if (writeFrame > (std::numeric_limits<Core::u64>::max)() - chunk.frameCount)
    {
        return fail(AudioErrorCode::CapacityExceeded,
                    "PCM stream frame counter overflowed");
    }

    const Core::usize channels = static_cast<Core::usize>(stream.channels);
    const Core::usize frameCount = static_cast<Core::usize>(chunk.frameCount);
    const Core::usize writeOffset =
        static_cast<Core::usize>(writeFrame % static_cast<Core::u64>(stream.capacityFrames));
    const Core::usize firstFrames =
        (std::min)(frameCount, stream.capacityFrames - writeOffset);
    std::memcpy(stream.frames + writeOffset * channels,
                chunk.frames,
                firstFrames * channels * sizeof(float));
    const Core::usize remainingFrames = frameCount - firstFrames;
    if (remainingFrames != 0)
    {
        std::memcpy(stream.frames,
                    chunk.frames + firstFrames * channels,
                    remainingFrames * channels * sizeof(float));
    }
    stream.writeFrame.store(writeFrame + chunk.frameCount, std::memory_order_release);
    return Core::success();
}

Core::Status AudioEngine::signalPcmStreamEof(AudioVoiceId voice) noexcept
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
    if (!record->hasStream || record->sourceKind != VoiceSourceKind::Stream)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "AudioVoiceId is not a PCM streaming voice");
    }
    auto& stream = m_impl->streamSlotFor(*record);
    if (stream.eofSignaled.load(std::memory_order_acquire))
    {
        return Core::success();
    }
    if (record->streamCancelQueued ||
        record->streamStopQueued ||
        stream.terminalCompletionPending)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "PCM stream stop or cancellation is already pending");
    }
    stream.eofSignaled.store(true, std::memory_order_release);
    return Core::success();
}

Core::Status AudioEngine::cancelPcmStream(AudioVoiceId voice) noexcept
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
    if (!record->hasStream || record->sourceKind != VoiceSourceKind::Stream)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "AudioVoiceId is not a PCM streaming voice");
    }
    auto& stream = m_impl->streamSlotFor(*record);
    if (record->streamCancelQueued ||
        (stream.terminalCompletionPending &&
         stream.terminalCompletion == AudioCompletionKind::Cancelled))
    {
        return Core::success();
    }
    if (stream.terminalCompletionPending)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "PCM stream terminal completion is already pending");
    }
    if (record->streamStopQueued)
    {
        return fail(AudioErrorCode::InvalidConfiguration,
                    "PCM stream stop is already pending");
    }
    if (Core::Status status = m_impl->enqueueCommand(AudioCommandKind::CancelStream, voice); !status)
    {
        return status;
    }
    record->streamCancelQueued = true;
    return Core::success();
}

Core::Result<AudioPcmStreamState> AudioEngine::pcmStreamState(AudioVoiceId voice) const noexcept
{
    if (m_impl == nullptr)
    {
        return Core::failure(AudioErrorCode::EngineClosed, "AudioEngine is closed");
    }
    if (!m_impl->isOwnerThread())
    {
        return Core::failure(AudioErrorCode::WrongOwnerThread,
                             "AudioEngine API must run on the owner thread");
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
    if (!record->hasStream || record->sourceKind != VoiceSourceKind::Stream)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration,
                             "AudioVoiceId is not a PCM streaming voice");
    }

    const auto& stream = m_impl->streamSlotFor(*record);
    const Core::u64 readFrame = stream.readFrame.load(std::memory_order_acquire);
    const Core::u64 writeFrame = stream.writeFrame.load(std::memory_order_acquire);
    const Core::u64 bufferedFrames = writeFrame >= readFrame ? writeFrame - readFrame : 0;
    return AudioPcmStreamState{
        .capacityFrames = stream.capacityFrames,
        .bufferedFrames = static_cast<Core::usize>(
            (std::min)(bufferedFrames, static_cast<Core::u64>(stream.capacityFrames))),
        .submittedFrames = writeFrame,
        .consumedFrames = readFrame,
        .underrunFrames = stream.underrunFrames.load(std::memory_order_relaxed),
        .channels = stream.channels,
        .sampleRate = stream.sampleRate,
        .playing = record->playing,
        .eofSignaled = stream.eofSignaled.load(std::memory_order_acquire),
        .cancelPending = record->streamCancelQueued ||
                         (stream.terminalCompletionPending &&
                          stream.terminalCompletion == AudioCompletionKind::Cancelled),
        .terminalCompletionPending = stream.terminalCompletionPending,
        .terminalCompletion = stream.terminalCompletion,
    };
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
    (void)m_impl->realtimeAdmissionState.fetch_or(Impl::RealtimeClosedBit,
                                                  std::memory_order_seq_cst);
    m_impl->commands.clear();
    m_impl->completions.clear();
    for (auto& slot : m_impl->mixSlots)
    {
        slot.active.store(false, std::memory_order_seq_cst);
        (void)slot.publicationGeneration.fetch_add(1, std::memory_order_seq_cst);
    }
    // Admission and closure share one atomic state, so every callback is either
    // rejected by the close bit or counted before shutdown can return.
    while ((m_impl->realtimeAdmissionState.load(std::memory_order_seq_cst) &
            Impl::RealtimeReaderMask) != 0)
    {
        std::this_thread::yield();
    }
    for (auto& slot : m_impl->mixSlots)
    {
        slot.finishedPublicationGeneration.store(0, std::memory_order_seq_cst);
        slot.frames.store(nullptr, std::memory_order_relaxed);
        slot.cursorFrame.store(0, std::memory_order_relaxed);
        slot.cursorFraction.store(0, std::memory_order_relaxed);
        slot.gainControlRevision.store(0, std::memory_order_relaxed);
        slot.appliedGainControlRevision.store(0, std::memory_order_relaxed);
        slot.fadeActive.store(false, std::memory_order_relaxed);
        slot.voice = {};
    }
    // Reset alongside streamSlots: shutdown does not promise to deliver terminals
    // that were never pumped, and a stale entry would otherwise outlive the voice
    // index it guards.
    for (auto& pending : m_impl->pendingClipTerminals)
    {
        pending = Impl::PendingClipTerminal{};
    }
    for (auto& stream : m_impl->streamSlots)
    {
        stream.capacityFrames = 0;
        stream.channels = 0;
        stream.sampleRate = 0;
        stream.configured = false;
        stream.terminalCompletionPending = false;
        stream.terminalVoice = {};
        stream.terminalCommandSequence = 0;
        stream.quiescingMixSlot = Impl::InvalidMixSlot;
        stream.quiescingPublicationGeneration = 0;
        stream.readFrame.store(0, std::memory_order_relaxed);
        stream.writeFrame.store(0, std::memory_order_relaxed);
        stream.eofSignaled.store(false, std::memory_order_relaxed);
    }
    m_impl->voices.clear();
    m_impl->boundClipVoices = 0;
    m_impl->streamingVoices = 0;
    m_impl->state = AudioEngineState::Stopped;
    m_impl->closed = true;
}

} // namespace Tina::Audio
