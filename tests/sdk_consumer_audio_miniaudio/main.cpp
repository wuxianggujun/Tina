#include <tina/audio/AudioDecode.hpp>
#include <tina/audio/miniaudio/MiniaudioDevice.hpp>

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    const Tina::Audio::AudioDecodeCapabilities capabilities = Tina::Audio::queryAudioDecodeCapabilities();
    if (!capabilities.wav || !capabilities.flac || !capabilities.mp3)
    {
        return 1;
    }

    auto device = Tina::Audio::MiniaudioDevice::Create(Tina::Audio::MiniaudioDeviceConfig{
        .useNullBackend = true,
        .sampleRate = 48000,
        .channels = 2,
        .periodFrames = 256,
    });
    if (!device || !device->isNullBackend() || device->isRunning())
    {
        return 1;
    }

    auto startStatus = device->start();
    if (!startStatus || !device->isRunning())
    {
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    while (device->callbackInvocations() == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    const Tina::Core::u64 callbackInvocations = device->callbackInvocations();

    device->stop();
    device->shutdown();
    if (callbackInvocations == 0 || device->isRunning())
    {
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"consumer\":\"installed-tina-audio-miniaudio\"}\n";
    return 0;
}
