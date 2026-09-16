#include <tina/audio/EncodedPcmStreamer.hpp>

#include <tina/audio/AudioErrors.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

namespace Tina::Audio {
namespace {

constexpr Core::u64 NoSeek = (std::numeric_limits<Core::u64>::max)();
constexpr Core::usize MaxQueuedChunks = 4;

[[nodiscard]] Core::u64 resolvedLoopEnd(const AudioPlayDesc& play, Core::u64 frameCount) noexcept
{
    return play.loopEndFrame == 0 ? frameCount : play.loopEndFrame;
}

} // namespace

struct EncodedPcmStreamer::Shared final {
    mutable std::mutex mutex{};
    std::condition_variable cv{};
    std::deque<std::vector<float>> chunks{};
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> eof{false};
    std::span<const std::byte> encoded{};
    AudioPlayDesc play{};
    Core::u64 loopStart = 0;
    Core::u64 loopEnd = 0;
    Core::usize chunkFrames = 2048;
    Core::u32 channels = 0;
    Core::u32 sampleRate = 0;
};

void EncodedPcmStreamer::decodeLoop(const std::shared_ptr<Shared>& shared)
{
    auto decoder = AudioDecoder::open(shared->encoded);
    if (!decoder)
    {
        shared->failed.store(true, std::memory_order_release);
        shared->cv.notify_all();
        return;
    }
    shared->channels = decoder->channels();
    shared->sampleRate = decoder->sampleRate();
    std::vector<float> pcm;
    try
    {
        pcm.resize(shared->chunkFrames * decoder->channels());
    }
    catch (...)
    {
        shared->failed.store(true, std::memory_order_release);
        shared->cv.notify_all();
        return;
    }
    while (!shared->stop.load(std::memory_order_acquire))
    {
        {
            std::unique_lock lock(shared->mutex);
            shared->cv.wait(lock, [&] {
                return shared->stop.load(std::memory_order_acquire) ||
                       shared->chunks.size() < MaxQueuedChunks;
            });
            if (shared->stop.load(std::memory_order_acquire))
            {
                return;
            }
        }
        auto frames = decoder->readPcm(pcm);
        if (!frames)
        {
            shared->failed.store(true, std::memory_order_release);
            shared->cv.notify_all();
            return;
        }
        if (*frames == 0)
        {
            if (shared->play.loopMode == AudioLoopMode::Loop)
            {
                if (auto status = decoder->seekFrame(shared->loopStart); !status)
                {
                    shared->failed.store(true, std::memory_order_release);
                    shared->cv.notify_all();
                    return;
                }
                continue;
            }
            shared->eof.store(true, std::memory_order_release);
            shared->cv.notify_all();
            std::unique_lock lock(shared->mutex);
            shared->cv.wait(lock, [&] { return shared->stop.load(std::memory_order_acquire); });
            return;
        }
        try
        {
            std::vector<float> chunk(pcm.begin(),
                                     pcm.begin() + static_cast<std::ptrdiff_t>(*frames * decoder->channels()));
            std::lock_guard lock(shared->mutex);
            shared->chunks.push_back(std::move(chunk));
        }
        catch (...)
        {
            shared->failed.store(true, std::memory_order_release);
            shared->cv.notify_all();
            return;
        }
        shared->cv.notify_all();
    }
}

EncodedPcmStreamer::EncodedPcmStreamer() noexcept = default;

EncodedPcmStreamer::EncodedPcmStreamer(EncodedPcmStreamer&& other) noexcept
    : m_engine(other.m_engine), m_voice(other.m_voice), m_shared(std::move(other.m_shared)),
      m_thread(std::move(other.m_thread))
{
    other.m_engine = nullptr;
    other.m_voice = {};
}

EncodedPcmStreamer& EncodedPcmStreamer::operator=(EncodedPcmStreamer&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    stopDecodeThread();
    m_engine = other.m_engine;
    m_voice = other.m_voice;
    m_shared = std::move(other.m_shared);
    m_thread = std::move(other.m_thread);
    other.m_engine = nullptr;
    other.m_voice = {};
    return *this;
}

EncodedPcmStreamer::~EncodedPcmStreamer()
{
    stopDecodeThread();
}

void EncodedPcmStreamer::stopDecodeThread() noexcept
{
    if (m_shared)
    {
        m_shared->stop.store(true, std::memory_order_release);
        m_shared->cv.notify_all();
    }
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

bool EncodedPcmStreamer::finished() const noexcept
{
    if (!m_shared) { return true; }
    if (m_shared->failed.load(std::memory_order_acquire)) { return true; }
    std::lock_guard lock(m_shared->mutex);
    return m_shared->eof.load(std::memory_order_acquire) && m_shared->chunks.empty();
}

Core::Result<EncodedPcmStreamer> EncodedPcmStreamer::Start(
    AudioEngine& engine, std::span<const std::byte> encoded, EncodedPcmStreamDesc desc) noexcept
try
{
    if (encoded.empty() || encoded.data() == nullptr || desc.bufferCapacityFrames < AudioPcmStreamMinBufferFrames ||
        desc.decodeChunkFrames == 0)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration,
                             "EncodedPcmStreamer requires encoded bytes and a valid stream buffer");
    }
    auto probe = AudioDecoder::open(encoded);
    if (!probe) { return Core::failure(std::move(probe.error())); }
    if (probe->channels() == 0 || probe->channels() > AudioPcmStreamMaxChannels)
    {
        return Core::failure(AudioErrorCode::NotSupported, "EncodedPcmStreamer channel count is unsupported");
    }
    const auto sourceFrames = desc.sourceFrameCount != 0 ? desc.sourceFrameCount : probe->frameCount();
    const auto loopEnd = resolvedLoopEnd(desc.play, sourceFrames);
    if (desc.play.loopMode == AudioLoopMode::Loop &&
        (loopEnd <= desc.play.loopStartFrame || (sourceFrames != 0 && loopEnd > sourceFrames)))
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration,
                             "EncodedPcmStreamer loop range is empty or past the clip");
    }

    auto shared = std::make_shared<Shared>();
    shared->encoded = encoded;
    shared->play = desc.play;
    shared->loopStart = desc.play.loopStartFrame;
    shared->loopEnd = loopEnd;
    shared->chunkFrames = desc.decodeChunkFrames;
    shared->channels = probe->channels();
    shared->sampleRate = probe->sampleRate();
    *probe = AudioDecoder{};

    auto voice = engine.playPcmStream(
        AudioPcmStreamDesc{
            .channels = shared->channels,
            .sampleRate = shared->sampleRate,
            .bufferCapacityFrames = desc.bufferCapacityFrames,
        },
        desc.bus);
    if (!voice) { return Core::failure(std::move(voice.error())); }

    EncodedPcmStreamer streamer;
    streamer.m_engine = &engine;
    streamer.m_voice = *voice;
    streamer.m_shared = std::move(shared);
    try
    {
        streamer.m_thread = std::thread{&EncodedPcmStreamer::decodeLoop, streamer.m_shared};
    }
    catch (...)
    {
        (void)engine.cancelPcmStream(*voice);
        return Core::failure(AudioErrorCode::ConstructionFailed, "EncodedPcmStreamer could not start a decode thread");
    }
    {
        std::unique_lock lock(streamer.m_shared->mutex);
        streamer.m_shared->cv.wait(lock, [&] {
            return streamer.m_shared->stop.load(std::memory_order_acquire) ||
                   streamer.m_shared->failed.load(std::memory_order_acquire) ||
                   streamer.m_shared->eof.load(std::memory_order_acquire) ||
                   !streamer.m_shared->chunks.empty();
        });
    }
    if (auto status = streamer.pump(); !status)
    {
        (void)engine.cancelPcmStream(*voice);
        return Core::failure(std::move(status.error()));
    }
    return streamer;
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "EncodedPcmStreamer allocation failed");
}

Core::Status EncodedPcmStreamer::pump() noexcept
{
    if (m_engine == nullptr || !m_voice.hasValue() || !m_shared)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "EncodedPcmStreamer is not started");
    }
    if (m_shared->failed.load(std::memory_order_acquire))
    {
        (void)m_engine->cancelPcmStream(m_voice);
        return Core::failure(AudioErrorCode::DecodeFailed, "EncodedPcmStreamer decode thread failed");
    }
    auto state = m_engine->pcmStreamState(m_voice);
    if (!state)
    {
        return Core::failure(std::move(state.error()));
    }
    if (state->eofSignaled || state->cancelPending || state->terminalCompletionPending)
    {
        return Core::success();
    }
    const auto watermark = (std::max)(state->capacityFrames / 2U, AudioPcmStreamMinBufferFrames);
    for (int fill = 0; fill < 8; ++fill)
    {
        auto latest = m_engine->pcmStreamState(m_voice);
        if (!latest) { return Core::failure(std::move(latest.error())); }
        if (latest->bufferedFrames >= watermark) { return Core::success(); }
        std::vector<float> chunk;
        {
            std::lock_guard lock(m_shared->mutex);
            if (m_shared->chunks.empty()) { break; }
            chunk = std::move(m_shared->chunks.front());
            m_shared->chunks.pop_front();
        }
        m_shared->cv.notify_all();
        if (chunk.empty() || m_shared->channels == 0) { continue; }
        const auto frames = chunk.size() / m_shared->channels;
        auto status = m_engine->submitPcmStreamFrames(
            m_voice, AudioPcmStreamChunkView{.frames = chunk.data(), .frameCount = frames});
        if (!status)
        {
            if (status.error().code == AudioErrorCode::CapacityExceeded)
            {
                std::lock_guard lock(m_shared->mutex);
                m_shared->chunks.push_front(std::move(chunk));
                return Core::success();
            }
            (void)m_engine->cancelPcmStream(m_voice);
            return status;
        }
    }
    if (m_shared->eof.load(std::memory_order_acquire))
    {
        std::lock_guard lock(m_shared->mutex);
        if (m_shared->chunks.empty())
        {
            return m_engine->signalPcmStreamEof(m_voice);
        }
    }
    return Core::success();
}

Core::Status EncodedPcmStreamer::cancel() noexcept
{
    stopDecodeThread();
    if (m_engine == nullptr || !m_voice.hasValue()) { return Core::success(); }
    return m_engine->cancelPcmStream(m_voice);
}

} // namespace Tina::Audio
