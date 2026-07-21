#include <tina/audio/miniaudio/MiniaudioDevice.hpp>

#include <tina/audio/AudioEngine.hpp>

#include <miniaudio.h>

#include <atomic>
#include <cstring>
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

void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
{
    static_cast<void>(input);
    static_cast<void>(frameCount);
    if (device == nullptr || device->pUserData == nullptr || output == nullptr)
    {
        return;
    }
    // Real-time: no allocation, no lock, no logging. Zero output + atomic count.
    auto* counter = static_cast<std::atomic<Core::u64>*>(device->pUserData);
    counter->fetch_add(1, std::memory_order_relaxed);
    // Silence: miniaudio expects the callback to fill output; we leave zeros.
    // Caller allocated output; do not free.
    const auto channels = device->playback.channels;
    const auto bytes = static_cast<size_t>(frameCount) * channels * sizeof(float);
    if (bytes > 0)
    {
        std::memset(output, 0, bytes);
    }
}

} // namespace

struct MiniaudioDevice::Impl final {
    Impl(MiniaudioDeviceConfig cfg, std::thread::id ownerThread) noexcept
        : config(cfg), owner(ownerThread)
    {
    }

    ~Impl() noexcept
    {
        release();
    }

    void release() noexcept
    {
        if (deviceInitialized)
        {
            if (running)
            {
                (void)ma_device_stop(&device);
                running = false;
            }
            ma_device_uninit(&device);
            deviceInitialized = false;
        }
        if (contextInitialized)
        {
            ma_context_uninit(&context);
            contextInitialized = false;
        }
    }

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }

    MiniaudioDeviceConfig config{};
    std::thread::id owner{};
    ma_context context{};
    ma_device device{};
    std::atomic<Core::u64> callbacks{0};
    bool contextInitialized = false;
    bool deviceInitialized = false;
    bool running = false;
    bool nullBackend = true;
};

Core::Result<MiniaudioDevice> MiniaudioDevice::Create(MiniaudioDeviceConfig config)
{
    if (config.sampleRate == 0 || config.channels == 0 || config.periodFrames == 0)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration,
                             "MiniaudioDevice sampleRate/channels/periodFrames must be > 0");
    }
    if (config.channels > 8)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "MiniaudioDevice channels must be <= 8");
    }

    std::unique_ptr<Impl> impl;
    try
    {
        impl = std::make_unique<Impl>(config, std::this_thread::get_id());
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AudioErrorCode::ConstructionFailed, "MiniaudioDevice allocation failed");
    }

    ma_context_config contextConfig = ma_context_config_init();
    ma_result contextResult = MA_ERROR;
    if (config.useNullBackend)
    {
        ma_backend backends[] = {ma_backend_null};
        contextResult = ma_context_init(backends, 1, &contextConfig, &impl->context);
        impl->nullBackend = true;
    }
    else
    {
        contextResult = ma_context_init(nullptr, 0, &contextConfig, &impl->context);
        impl->nullBackend = false;
    }
    if (contextResult != MA_SUCCESS)
    {
        return Core::failure(AudioErrorCode::BackendFailure, "ma_context_init failed");
    }
    impl->contextInitialized = true;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = config.channels;
    deviceConfig.sampleRate = config.sampleRate;
    deviceConfig.periodSizeInFrames = config.periodFrames;
    deviceConfig.dataCallback = dataCallback;
    deviceConfig.pUserData = &impl->callbacks;

    const ma_result deviceResult = ma_device_init(&impl->context, &deviceConfig, &impl->device);
    if (deviceResult != MA_SUCCESS)
    {
        impl->release();
        return Core::failure(AudioErrorCode::BackendFailure, "ma_device_init failed");
    }
    impl->deviceInitialized = true;

    return MiniaudioDevice(impl.release());
}

MiniaudioDevice::MiniaudioDevice(Impl* impl) noexcept : m_impl(impl) {}

MiniaudioDevice::~MiniaudioDevice() noexcept
{
    shutdown();
    delete m_impl;
    m_impl = nullptr;
}

MiniaudioDevice::MiniaudioDevice(MiniaudioDevice&& other) noexcept : m_impl(std::exchange(other.m_impl, nullptr)) {}

Core::Status MiniaudioDevice::start() noexcept
{
    if (m_impl == nullptr)
    {
        return fail(AudioErrorCode::EngineClosed, "MiniaudioDevice is closed");
    }
    if (!m_impl->isOwnerThread())
    {
        return fail(AudioErrorCode::WrongOwnerThread, "MiniaudioDevice API must run on the owner thread");
    }
    if (!m_impl->deviceInitialized)
    {
        return fail(AudioErrorCode::EngineClosed, "MiniaudioDevice is not initialized");
    }
    if (m_impl->running)
    {
        return Core::success();
    }
    const ma_result result = ma_device_start(&m_impl->device);
    if (result != MA_SUCCESS)
    {
        return fail(AudioErrorCode::BackendFailure, "ma_device_start failed");
    }
    m_impl->running = true;
    return Core::success();
}

void MiniaudioDevice::stop() noexcept
{
    if (m_impl == nullptr || !m_impl->deviceInitialized || !m_impl->running)
    {
        return;
    }
    (void)ma_device_stop(&m_impl->device);
    m_impl->running = false;
}

void MiniaudioDevice::shutdown() noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }
    m_impl->release();
}

bool MiniaudioDevice::isRunning() const noexcept
{
    return m_impl != nullptr && m_impl->running;
}

bool MiniaudioDevice::isNullBackend() const noexcept
{
    return m_impl != nullptr && m_impl->nullBackend;
}

Core::u32 MiniaudioDevice::sampleRate() const noexcept
{
    return m_impl != nullptr ? m_impl->config.sampleRate : 0;
}

Core::u32 MiniaudioDevice::channels() const noexcept
{
    return m_impl != nullptr ? m_impl->config.channels : 0;
}

Core::u64 MiniaudioDevice::callbackInvocations() const noexcept
{
    if (m_impl == nullptr)
    {
        return 0;
    }
    return m_impl->callbacks.load(std::memory_order_relaxed);
}

Core::Result<MiniaudioAudioBundle> createMiniaudioAudioBundle(AudioEngineConfig engineConfig,
                                                              MiniaudioDeviceConfig deviceConfig,
                                                              std::pmr::memory_resource& resource)
{
    auto engine = AudioEngine::Create(engineConfig, resource);
    if (!engine)
    {
        return Core::failure(engine.error());
    }
    auto device = MiniaudioDevice::Create(deviceConfig);
    if (!device)
    {
        engine->shutdown();
        return Core::failure(device.error());
    }
    return MiniaudioAudioBundle{
        .engine = std::move(*engine),
        .device = std::move(*device),
    };
}

} // namespace Tina::Audio
