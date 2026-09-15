#include <tina/audio/miniaudio/MiniaudioDevice.hpp>

#include <tina/audio/AudioEngine.hpp>

#include <miniaudio.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <expected>
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

[[nodiscard]] std::unexpected<Core::Error> failMa(Core::ErrorCode code, const char* prefix, ma_result result) noexcept
{
    char message[256];
    const char* detail = ma_result_description(result);
    std::snprintf(message, sizeof(message), "%s: %s", prefix, detail != nullptr ? detail : "unknown");
    return Core::failure(code, message);
}

struct DeviceCallbackUserData final {
    std::atomic<Core::u64>* callbacks = nullptr;
    std::atomic<AudioEngine*>* mixer = nullptr;
    Core::u32 channels = 0;
    Core::u32 sampleRate = 0;
};

void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
{
    static_cast<void>(input);
    if (device == nullptr || device->pUserData == nullptr || output == nullptr)
    {
        return;
    }
    auto* user = static_cast<DeviceCallbackUserData*>(device->pUserData);
    if (user->callbacks != nullptr)
    {
        user->callbacks->fetch_add(1, std::memory_order_relaxed);
    }
    const auto channels = device->playback.channels != 0 ? device->playback.channels : user->channels;
    const auto sampleRate = device->sampleRate != 0 ? device->sampleRate : user->sampleRate;
    auto* out = static_cast<float*>(output);
    AudioEngine* engine = user->mixer != nullptr ? user->mixer->load(std::memory_order_acquire) : nullptr;
    if (engine != nullptr)
    {
        engine->mixRealtime(out, frameCount, channels, sampleRate);
        return;
    }
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
        callbackUser.callbacks = &callbacks;
        callbackUser.mixer = &mixerEngine;
        callbackUser.channels = cfg.channels;
        callbackUser.sampleRate = cfg.sampleRate;
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
        mixerEngine.store(nullptr, std::memory_order_release);
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
    std::atomic<AudioEngine*> mixerEngine{nullptr};
    DeviceCallbackUserData callbackUser{};
    bool contextInitialized = false;
    bool deviceInitialized = false;
    bool running = false;
    bool nullBackend = true;
    const char* backendName = "";
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

    const auto initContext = [&](const ma_backend* backends, ma_uint32 backendCount) -> ma_result {
        ma_context_config contextConfig = ma_context_config_init();
        return ma_context_init(backends, backendCount, &contextConfig, &impl->context);
    };
    const auto initDevice = [&]() -> ma_result {
        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.format = ma_format_f32;
        deviceConfig.playback.channels = config.channels;
        deviceConfig.sampleRate = config.sampleRate;
        deviceConfig.periodSizeInFrames = config.periodFrames;
        deviceConfig.dataCallback = dataCallback;
        deviceConfig.pUserData = &impl->callbackUser;
        return ma_device_init(&impl->context, &deviceConfig, &impl->device);
    };

    ma_result contextResult = MA_ERROR;
    if (config.useNullBackend)
    {
        const ma_backend backends[] = {ma_backend_null};
        contextResult = initContext(backends, 1);
        impl->nullBackend = true;
    }
#if defined(__ANDROID__)
    else
    {
        // AAudio is API 26+ and loaded by dlopen. OpenSL ES is the API 24 baseline.
        const ma_backend backends[] = {ma_backend_aaudio, ma_backend_opensl};
        contextResult = initContext(backends, 2);
        impl->nullBackend = false;
    }
#else
    else
    {
        contextResult = initContext(nullptr, 0);
        impl->nullBackend = false;
    }
#endif
    if (contextResult != MA_SUCCESS)
    {
        return failMa(AudioErrorCode::BackendFailure, "ma_context_init failed", contextResult);
    }
    impl->contextInitialized = true;
    impl->backendName = ma_get_backend_name(impl->context.backend);

    ma_result deviceResult = initDevice();
#if defined(__ANDROID__)
    if (deviceResult != MA_SUCCESS && !config.useNullBackend && impl->context.backend == ma_backend_aaudio)
    {
        impl->release();
        const ma_backend opensl[] = {ma_backend_opensl};
        contextResult = initContext(opensl, 1);
        if (contextResult != MA_SUCCESS)
        {
            return failMa(AudioErrorCode::BackendFailure, "ma_context_init failed", contextResult);
        }
        impl->contextInitialized = true;
        impl->nullBackend = false;
        impl->backendName = ma_get_backend_name(impl->context.backend);
        deviceResult = initDevice();
    }
#endif
    if (deviceResult != MA_SUCCESS)
    {
        impl->release();
        return failMa(AudioErrorCode::BackendFailure, "ma_device_init failed", deviceResult);
    }
    impl->deviceInitialized = true;
    impl->callbackUser.channels = impl->device.playback.channels;
    impl->callbackUser.sampleRate = impl->device.sampleRate;

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

MiniaudioDevice& MiniaudioDevice::operator=(MiniaudioDevice&& other) noexcept
{
    if (this != &other)
    {
        shutdown();
        delete m_impl;
        m_impl = std::exchange(other.m_impl, nullptr);
    }
    return *this;
}

void MiniaudioDevice::attachMixer(AudioEngine* engine) noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }
    m_impl->mixerEngine.store(engine, std::memory_order_release);
}

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
        return failMa(AudioErrorCode::BackendFailure, "ma_device_start failed", result);
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

const char* MiniaudioDevice::backendName() const noexcept
{
    if (m_impl == nullptr)
    {
        return "";
    }
    if (m_impl->backendName != nullptr && m_impl->backendName[0] != '\0')
    {
        return m_impl->backendName;
    }
    return m_impl->nullBackend ? "null" : "";
}

Core::u32 MiniaudioDevice::sampleRate() const noexcept
{
    if (m_impl == nullptr)
    {
        return 0;
    }
    if (m_impl->deviceInitialized)
    {
        return static_cast<Core::u32>(m_impl->device.sampleRate);
    }
    return m_impl->config.sampleRate;
}

Core::u32 MiniaudioDevice::channels() const noexcept
{
    if (m_impl == nullptr)
    {
        return 0;
    }
    if (m_impl->deviceInitialized)
    {
        return static_cast<Core::u32>(m_impl->device.playback.channels);
    }
    return m_impl->config.channels;
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
    return MiniaudioAudioBundle{std::move(*engine), std::move(*device)};
}

} // namespace Tina::Audio
