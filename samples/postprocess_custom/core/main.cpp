#include "Sample.hpp"
#include <tina/core/text/ParseInteger.hpp>

#include <tina/asset/AssetGpuShader.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/ShaderPayload.hpp>
#include <tina/core/io/ApplicationPaths.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderPostProcess.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string_view>

namespace {
using namespace Tina;

constexpr std::string_view SampleName = "tina_sample_postprocess_custom";
constexpr float SceneGray = 0.25F;
constexpr std::array<float, 4> GradeA{3.0F, 0.2F, 0.2F, 1.0F};
constexpr std::array<float, 4> GradeB{0.2F, 3.0F, 0.2F, 1.0F};
constexpr std::array<float, 4> SecondGrade{0.5F, 1.0F, 0.5F, 1.0F};
constexpr Core::u64 FirstCaptureFrame = 5;
constexpr Core::u64 SecondCaptureFrame = 9;
constexpr Core::u64 VerificationFrameCount = 24;
constexpr float MaximumChannelError = 6.0F;

struct Options final {
    Core::u64 frames = 0; // Interactive until the window closes unless explicitly bounded.
    bool verify = false;
};

struct Counters final {
    Core::u64 frames = 0;
    Core::u32 verifiedCaptures = 0;
    float maximumChannelError = 0.0F;
    bool shaderRetired = false;
    std::optional<Core::Error> cleanupError{};
};

void writeError(const Core::Error& error)
{
    Core::JsonWriter writer{std::cerr};
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", SampleName);
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
}

Core::Result<Options> parseOptions(int argumentCount, char** arguments)
{
    Options options{};
    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "--verify") { options.verify = true; continue; }
        if (argument.starts_with("--frames=")) {
            const auto value = argument.substr(std::string_view{"--frames="}.size());
            if (Core::parseUnsigned(value, options.frames) && options.frames > 0)
                continue;
        }
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Use --frames=<positive integer> and optional --verify");
    }
    if (options.verify && options.frames == 0) options.frames = VerificationFrameCount;
    if (options.verify && options.frames <= SecondCaptureFrame)
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Post-process pixel verification requires at least 10 frames");
    return options;
}

Core::Result<Asset::CookedAssetFile> loadShader()
{
    auto path = Core::applicationFilePath("fs_post_grade.shaderpayload");
    if (!path) return Core::failure(std::move(path.error()));
    auto payload = Core::readFile(*path, {.maxBytes = AssetFormat::ShaderWire::MaxPayloadBytes,
                                        .memoryResource = std::pmr::get_default_resource()});
    if (!payload) return Core::failure(std::move(payload.error()));
    auto parsed = AssetFormat::parseShaderPayload(*payload);
    if (!parsed) return Core::failure(std::move(parsed.error()));
    if (parsed->shaderKind != AssetFormat::ShaderKind::PostProcess)
        return Core::failure(Render::RenderErrorCode::InvalidShaderUpload,
                             "The sample requires a PostProcess shader payload");
    Core::AssetId::Bytes idBytes{};
    idBytes.front() = std::byte{0x55};
    auto id = Core::AssetId::fromBytes(idBytes);
    if (!id) return Core::failure(Core::CoreErrorCode::Internal, "Invalid sample shader identity");
    auto cooked = AssetFormat::writeCookedAssetBytes({
        .assetKind = AssetFormat::AssetKind::Shader,
        .assetTypeVersion = AssetFormat::ShaderWire::SchemaVersion,
        .assetId = *id,
        .payload = *payload,
    });
    if (!cooked) return Core::failure(std::move(cooked.error()));
    std::pmr::vector<std::byte> owned{std::pmr::get_default_resource()};
    owned.assign(cooked->begin(), cooked->end());
    return Asset::makeCookedAssetFileFromBytes(std::move(owned), {});
}

Render::GpuShaderUniformValue gradeValue(const std::array<float, 4>& grade) noexcept
{
    Render::GpuShaderUniformValue value{};
    constexpr std::string_view name = "u_grade";
    std::copy(name.begin(), name.end(), value.name.begin());
    value.value = grade;
    return value;
}

class PostProcessState final : public IGameState {
  public:
    PostProcessState(Options options, Counters& counters) noexcept : options_(options), counters_(counters) {}
    ~PostProcessState() noexcept override { recordCleanup(release()); }

    Core::Status onEnter(GameStateEnterContext& context) override
    {
        device_ = &context.renderDevice();
        auto asset = loadShader();
        if (!asset) return Core::failure(std::move(asset.error()));
        auto shader = Asset::uploadShaderFromCooked(*device_, *asset);
        if (!shader) return Core::failure(std::move(shader.error()));
        shader_ = *shader;
        auto key = device_->createShaderBinding(shader_);
        if (!key) return Core::failure(std::move(key.error()));
        shaderKey_ = *key;
        const std::array grades{gradeValue(GradeA), gradeValue(SecondGrade)};
        for (Core::usize index = 0; index < grades.size(); ++index) {
            auto uniformKey = device_->createShaderUniformBinding({std::span{&grades[index], 1}});
            if (!uniformKey) return Core::failure(std::move(uniformKey.error()));
            uniformKeys_[index] = *uniformKey;
        }
        return Core::success();
    }

    void onExit(GameStateExitContext&) noexcept override { recordCleanup(release()); }

    Core::Status updateFrame(FrameUpdateContext& context) override
    {
        ++counters_.frames;
        if (options_.verify && (counters_.frames == FirstCaptureFrame + 1 ||
                                counters_.frames == SecondCaptureFrame + 1)) {
            auto capture = device_->collectPrimaryFrameCapture();
            if (!capture) return Core::failure(std::move(capture.error()));
            if (auto status = verifyCapture(*capture, counters_.frames == FirstCaptureFrame + 1 ? GradeA : GradeB);
                !status) return status;
        }
        const bool phaseB = options_.verify ? counters_.frames >= SecondCaptureFrame
                                            : (counters_.frames / 180U) % 2U != 0;
        const auto grade = gradeValue(phaseB ? GradeB : GradeA);
        if (auto status = device_->setShaderUniformBinding(uniformKeys_[0], {std::span{&grade, 1}}); !status)
            return status;
        if (options_.verify && (counters_.frames == FirstCaptureFrame || counters_.frames == SecondCaptureFrame))
            if (auto status = device_->requestPrimaryFrameCaptureOnNextPresent(); !status) return status;
        if (options_.frames != 0 && counters_.frames >= options_.frames) context.requestExitAfterFrame();
        return Core::success();
    }

    Core::Status extractRenderScene(RenderSceneExtractionContext& context) const override
    {
        if (auto status = context.renderSceneWriter().setClearColor({SceneGray, SceneGray, SceneGray, 1.0F});
            !status) return status;
        auto shader = intern(context.frameResourceSink(), Render::FrameResourceKind::Shader, shaderKey_);
        if (!shader) return Core::failure(std::move(shader.error()));
        Render::PrimaryPostProcessSettings settings{};
        settings.enabled = true;
        settings.customEffectCount = static_cast<Core::u8>(uniformKeys_.size());
        for (Core::usize index = 0; index < uniformKeys_.size(); ++index) {
            auto uniforms = intern(context.frameResourceSink(), Render::FrameResourceKind::ShaderUniforms,
                                   uniformKeys_[index]);
            if (!uniforms) return Core::failure(std::move(uniforms.error()));
            settings.customEffects[index] = {*shader, *uniforms};
        }
        return context.setPrimaryPostProcess(settings);
    }

  private:
    Core::Result<Render::FrameResourceRef> intern(Render::FrameResourceSink& sink,
                                                 Render::FrameResourceKind kind, Core::u32 key) const noexcept
    {
        ++borrows_;
        Render::FramePin pin{Render::FramePinKind::Custom, key, const_cast<PostProcessState*>(this),
            [](void* pointer) noexcept {
                auto& state = *static_cast<PostProcessState*>(pointer);
                if (state.borrows_ == 0) std::terminate();
                --state.borrows_;
            }};
        return sink.intern({.kind = kind, .deviceBindingKey = key}, std::move(pin));
    }

    Core::Status verifyCapture(const Render::Rgba8FrameCapture& capture,
                               const std::array<float, 4>& firstGrade)
    {
        if (capture.empty() || static_cast<Core::u64>(capture.width) * capture.height * 4U != capture.byteCount())
            return Core::failure(Core::CoreErrorCode::Internal, "Invalid post-process capture extent");
        const auto expected = Render::toneMapLinearColor(
            {SceneGray * firstGrade[0] * SecondGrade[0], SceneGray * firstGrade[1] * SecondGrade[1],
             SceneGray * firstGrade[2] * SecondGrade[2], 1.0F}, {});
        const std::array channels{expected.r, expected.g, expected.b};
        const Core::usize offset = (static_cast<Core::usize>(capture.height / 2U) * capture.width +
                                    capture.width / 2U) * 4U;
        for (Core::usize channel = 0; channel < channels.size(); ++channel) {
            const auto actual = std::to_integer<Core::u8>(capture.rgba8Pixels[offset + channel]);
            counters_.maximumChannelError = (std::max)(counters_.maximumChannelError,
                std::abs(static_cast<float>(actual) - channels[channel] * 255.0F));
        }
        if (counters_.maximumChannelError > MaximumChannelError)
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "Post-process pixels disagree with two independent materials and one output transform");
        ++counters_.verifiedCaptures;
        return Core::success();
    }

    Core::Status release() noexcept
    {
        if (device_ == nullptr) return Core::success();
        // The State owns these native resources; frame pins must be returned
        // before Runtime allows the owner to leave, including failure rollback.
        if (borrows_ != 0) std::terminate();
        if (shader_) {
            if (auto status = device_->destroyShader(shader_); !status) return status;
            shader_ = {};
            shaderKey_ = 0;
            counters_.shaderRetired = true;
        }
        for (auto& key : uniformKeys_) {
            if (key == 0) continue;
            if (auto status = device_->setShaderUniformBinding(key, {}); !status) return status;
            key = 0;
        }
        device_ = nullptr;
        return Core::success();
    }

    void recordCleanup(Core::Status status) noexcept
    {
        if (!status && !counters_.cleanupError) counters_.cleanupError = std::move(status.error());
    }

    Options options_{};
    Counters& counters_;
    Render::IRenderDevice* device_ = nullptr;
    Render::GpuShaderId shader_{};
    Core::u32 shaderKey_ = 0;
    std::array<Core::u32, 2> uniformKeys_{};
    mutable Core::u32 borrows_ = 0;
};

class PostProcessApplication final : public IGameApplication {
  public:
    PostProcessApplication(Options options, Counters& counters) noexcept : options_(options), counters_(counters) {}
    Core::Result<std::unique_ptr<IGameState>> createInitialState(GameStartupContext&) override
    {
        std::unique_ptr<IGameState> state = std::make_unique<PostProcessState>(options_, counters_);
        return state;
    }
  private:
    Options options_{};
    Counters& counters_;
};
} // namespace

int runPostProcessCustomSample(int argumentCount, char** arguments)
try {
    auto options = parseOptions(argumentCount, arguments);
    if (!options) { writeError(options.error()); return 2; }
    Counters counters{};
    PostProcessApplication application{*options, counters};
    EngineConfig config = EngineConfig::Defaults();
    config.applicationName = "Tina PostProcess Shader";
    config.primaryWindow.title = "Tina - Custom PostProcess / independent materials";
    config.primaryWindow.initialLogicalExtent = {960, 600};
    auto host = Desktop::CreateEngine(config);
    if (!host) { writeError(host.error()); return 1; }
    auto result = (*host)->run(application);
    host->reset();
    if (!result) { writeError(result.error()); return 1; }
    if (counters.cleanupError) { writeError(*counters.cleanupError); return 1; }
    if (!counters.shaderRetired || (options->verify && counters.verifiedCaptures != 2)) {
        writeError(Core::Error{Core::CoreErrorCode::Internal, "Post-process sample did not complete its requested lifecycle"});
        return 1;
    }
    Core::JsonWriter writer{std::cout};
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("sample", SampleName);
    writer.member("frames", counters.frames);
    writer.member("verifiedCaptures", counters.verifiedCaptures);
    writer.member("maximumChannelError", counters.maximumChannelError);
    writer.member("shaderRetired", counters.shaderRetired);
    writer.endObject();
    std::cout << '\n';
    return 0;
} catch (const std::bad_alloc&) {
    writeError(Core::Error{Core::CoreErrorCode::OutOfMemory, "Post-process sample allocation failed"});
    return 1;
} catch (const std::exception& exception) {
    writeError(Core::Error{Core::CoreErrorCode::Internal, exception.what()});
    return 1;
}
