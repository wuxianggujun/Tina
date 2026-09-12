#include <tina/audio/AudioDecode.hpp>
#include <tina/audio/miniaudio/MiniaudioDevice.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/core/io/ApplicationPaths.hpp>
#include "../support/AudioFixtures.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory_resource>
#include <thread>

int main()
{
    const Tina::Audio::AudioDecodeCapabilities capabilities = Tina::Audio::queryAudioDecodeCapabilities();
    if (!capabilities.wav || !capabilities.flac || !capabilities.mp3 || !capabilities.oggVorbis || !capabilities.opus)
    {
        return 1;
    }

    for (const auto* name : Tina::Tests::AudioFixtureNames) {
        auto pcm = Tina::Audio::decodeAudioMemory(Tina::Tests::readAudioFixture(name));
        if (!pcm || pcm->channels() != 2 || pcm->sampleRate() != 48000 || pcm->frameCount() != 4800 ||
            !std::any_of(pcm->interleavedPcm().begin(), pcm->interleavedPcm().end(),
                         [](float sample) { return std::isfinite(sample) && std::abs(sample) > 0.1F; })) {
            std::cerr << "Installed decoder failed: " << name << '\n';
            return 2;
        }
    }
    auto root = Tina::Core::applicationFilePath("audio-catalog");
    if (!root) { return 3; }
    std::pmr::unsynchronized_pool_resource memory;
    auto catalog = Tina::Asset::openCatalogPackage(*root, {
        .manifest = {.catalog = {.maxEntries = 8, .maxDependencies = 8,
                                 .maxDependenciesPerAsset = 8, .memoryResource = &memory}},
        .validation = {.file = {.memoryResource = &memory}, .verifyTypedPayload = true},
    });
    if (!catalog || catalog->entryCount() != 2) {
        std::cerr << "Installed AUDIOS helper did not publish the expected complete catalog\n";
        return 3;
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
