#include <tina/core/error/Error.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Tina::Core::u64;

// ADR 0018 schema v1 — first product slice (provisional on shared machines).
inline constexpr int kSchemaVersion = 1;
inline constexpr std::string_view kWorkloadId = "null_runtime_frames";
inline constexpr int kWorkloadVersion = 1;

struct Options final {
    u64 warmUpFrames = 60;
    u64 measureFrames = 600;
    u64 seed = 1;
    bool help = false;
};

struct Counters final {
    u64 frameUpdates = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
};

[[nodiscard]] bool parseU64(std::string_view text, u64& out) noexcept
{
    unsigned long long value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    out = static_cast<u64>(value);
    return true;
}

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options, std::string& error)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            return true;
        }
        constexpr std::string_view warmPrefix = "--warmup=";
        constexpr std::string_view samplesPrefix = "--samples=";
        constexpr std::string_view seedPrefix = "--seed=";
        constexpr std::string_view workloadPrefix = "--workload=";
        if (argument.starts_with(warmPrefix)) {
            if (!parseU64(argument.substr(warmPrefix.size()), options.warmUpFrames)) {
                error = "invalid --warmup value";
                return false;
            }
            continue;
        }
        if (argument.starts_with(samplesPrefix)) {
            if (!parseU64(argument.substr(samplesPrefix.size()), options.measureFrames)
                || options.measureFrames == 0) {
                error = "invalid --samples value";
                return false;
            }
            continue;
        }
        if (argument.starts_with(seedPrefix)) {
            if (!parseU64(argument.substr(seedPrefix.size()), options.seed)) {
                error = "invalid --seed value";
                return false;
            }
            continue;
        }
        if (argument.starts_with(workloadPrefix)) {
            const auto id = argument.substr(workloadPrefix.size());
            if (id != kWorkloadId) {
                error = "unknown workload (supported: null_runtime_frames)";
                return false;
            }
            continue;
        }
        error = "unknown argument: ";
        error.append(argument);
        return false;
    }
    return true;
}

[[nodiscard]] u64 nearestRankNs(std::vector<u64>& samples, double quantile) noexcept
{
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const double rank = quantile * static_cast<double>(samples.size() - 1);
    const std::size_t index = static_cast<std::size_t>(std::llround(rank));
    return samples[(std::min)(index, samples.size() - 1)];
}

// FNV-1a 64 over ASCII fields for counter/workload fingerprint (not timing).
[[nodiscard]] u64 fnv1a64(std::string_view data) noexcept
{
    u64 hash = 14695981039346656037ULL;
    for (const unsigned char c : data) {
        hash ^= static_cast<u64>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string hex64(u64 value)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[value & 0xFULL];
        value >>= 4;
    }
    return out;
}

class BenchGameState final : public Tina::IGameState {
public:
    BenchGameState(u64 targetFrameCount, Counters& counters, std::vector<u64>* frameNs) noexcept
        : m_targetFrameCount(targetFrameCount), m_counters(counters), m_frameNs(frameNs)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override { return Tina::Core::success(); }

    void onExit(Tina::GameStateExitContext&) noexcept override { ++m_counters.stateExits; }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        const auto begin = m_clock.now();
        ++m_counters.frameUpdates;
        // Tiny deterministic checksum work scaled by seed (keeps workload non-empty).
        volatile u64 sink = context.frameTiming().frameIndex ^ m_seed;
        sink = sink * 0x9E3779B97F4A7C15ULL + m_counters.frameUpdates;
        (void)sink;
        const auto end = m_clock.now();
        if (m_frameNs != nullptr && m_counters.frameUpdates > m_warmUp) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);
            m_frameNs->push_back(static_cast<u64>((std::max)(elapsed.count(), std::int64_t{0})));
        }
        if (context.frameTiming().frameIndex + 1U == m_targetFrameCount) {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    void setWarmUp(u64 warmUp) noexcept { m_warmUp = warmUp; }
    void setSeed(u64 seed) noexcept { m_seed = seed; }

private:
    u64 m_targetFrameCount = 0;
    u64 m_warmUp = 0;
    u64 m_seed = 1;
    Counters& m_counters;
    std::vector<u64>* m_frameNs = nullptr;
    Tina::Core::SteadyMonotonicClock m_clock{};
};

class BenchGameApplication final : public Tina::IGameApplication {
public:
    BenchGameApplication(u64 targetFrameCount, u64 warmUp, u64 seed, Counters& counters,
                         std::vector<u64>& frameNs) noexcept
        : m_targetFrameCount(targetFrameCount)
        , m_warmUp(warmUp)
        , m_seed(seed)
        , m_counters(counters)
        , m_frameNs(frameNs)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        auto state = std::make_unique<BenchGameState>(m_targetFrameCount, m_counters, &m_frameNs);
        state->setWarmUp(m_warmUp);
        state->setSeed(m_seed);
        return std::unique_ptr<Tina::IGameState>{std::move(state)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override { ++m_counters.applicationShutdowns; }

private:
    u64 m_targetFrameCount = 0;
    u64 m_warmUp = 0;
    u64 m_seed = 1;
    Counters& m_counters;
    std::vector<u64>& m_frameNs;
};

void printHelp()
{
    std::cout
        << "tina_bench (ADR 0018 schema v1, provisional)\n"
        << "  --workload=null_runtime_frames  fixed Null Runtime frame loop (default)\n"
        << "  --warmup=N                      frames excluded from quantiles (default 60)\n"
        << "  --samples=N                     measured frames after warmup (default 600)\n"
        << "  --seed=N                        workload seed mixed into per-frame work (default 1)\n"
        << "  -h, --help\n"
        << "Note: shared CI/dev machines produce provisional conclusions only.\n";
}

} // namespace

int main(int argc, char** argv)
{
    Options options{};
    std::string parseError;
    if (!parseOptions(argc, argv, options, parseError)) {
        std::cerr << "{\"status\":\"error\",\"schema\":" << kSchemaVersion
                  << ",\"message\":\"" << parseError << "\"}\n";
        return 2;
    }
    if (options.help) {
        printHelp();
        return 0;
    }

    const u64 totalFrames = options.warmUpFrames + options.measureFrames;
    Counters counters{};
    std::vector<u64> frameNs;
    frameNs.reserve(static_cast<std::size_t>(options.measureFrames));

    Tina::EngineCompositionFactories factories{};
    factories.createMonotonicClock =
        []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
        return std::unique_ptr<Tina::Core::IMonotonicClock>{
            std::make_unique<Tina::Core::SteadyMonotonicClock>()};
    };
    factories.createTaskSystem = Tina::Task::createDisabledTaskSystem;
    factories.platformRender = Tina::IndependentPlatformRenderFactories{
        .createPlatformBackend = Tina::Platform::createHeadlessPlatformBackend,
        .createRenderDevice = Tina::Render::createNullRenderDevice,
    };

    auto hostResult = Tina::EngineHost::Create(Tina::EngineConfig::Defaults(), std::move(factories));
    if (!hostResult) {
        std::cerr << "{\"status\":\"error\",\"schema\":" << kSchemaVersion
                  << ",\"message\":\"EngineHost::Create failed\"}\n";
        return 1;
    }

    BenchGameApplication app{totalFrames, options.warmUpFrames, options.seed, counters, frameNs};
    const auto wallBegin = Tina::Core::SteadyMonotonicClock{}.now();
    auto runResult = (*hostResult)->run(app);
    const auto wallEnd = Tina::Core::SteadyMonotonicClock{}.now();
    if (!runResult) {
        std::cerr << "{\"status\":\"error\",\"schema\":" << kSchemaVersion
                  << ",\"message\":\"EngineHost::run failed\"}\n";
        return 1;
    }

    const auto wallNs = static_cast<u64>((std::max)(
        std::chrono::duration_cast<std::chrono::nanoseconds>(wallEnd - wallBegin).count(),
        std::int64_t{0}));

    std::vector<u64> quantiles = frameNs;
    const u64 p50 = nearestRankNs(quantiles, 0.50);
    quantiles = frameNs;
    const u64 p95 = nearestRankNs(quantiles, 0.95);
    quantiles = frameNs;
    const u64 p99 = nearestRankNs(quantiles, 0.99);
    u64 maxNs = 0;
    u64 sumNs = 0;
    for (const u64 sample : frameNs) {
        sumNs += sample;
        maxNs = (std::max)(maxNs, sample);
    }
    const double meanNs =
        frameNs.empty() ? 0.0 : static_cast<double>(sumNs) / static_cast<double>(frameNs.size());

    std::string checksumInput;
    checksumInput.reserve(128);
    checksumInput.append(kWorkloadId);
    checksumInput.push_back('|');
    checksumInput.append(std::to_string(kWorkloadVersion));
    checksumInput.push_back('|');
    checksumInput.append(std::to_string(options.seed));
    checksumInput.push_back('|');
    checksumInput.append(std::to_string(options.warmUpFrames));
    checksumInput.push_back('|');
    checksumInput.append(std::to_string(options.measureFrames));
    checksumInput.push_back('|');
    checksumInput.append(std::to_string(counters.frameUpdates));
    checksumInput.push_back('|');
    checksumInput.append(std::to_string(counters.stateExits));
    checksumInput.push_back('|');
    checksumInput.append(std::to_string(counters.applicationShutdowns));
    const std::string checksum = hex64(fnv1a64(checksumInput));

#if defined(NDEBUG)
    constexpr const char* kBuildType = "Release";
#else
    constexpr const char* kBuildType = "Debug";
#endif

#if defined(_WIN32)
    constexpr const char* kHostOs = "windows";
#elif defined(__linux__)
    constexpr const char* kHostOs = "linux";
#else
    constexpr const char* kHostOs = "unknown";
#endif

    std::cout << "{"
              << "\"status\":\"ok\","
              << "\"schema\":" << kSchemaVersion << ','
              << "\"schemaName\":\"tina_bench\","
              << "\"conclusion\":\"provisional\","
              << "\"workload\":{"
              << "\"id\":\"" << kWorkloadId << "\","
              << "\"version\":" << kWorkloadVersion << ','
              << "\"seed\":" << options.seed << ','
              << "\"parameters\":{"
              << "\"warmup_frames\":" << options.warmUpFrames << ','
              << "\"measure_frames\":" << options.measureFrames
              << "}"
              << "},"
              << "\"fingerprint\":{"
              << "\"buildType\":\"" << kBuildType << "\","
              << "\"hostOs\":\"" << kHostOs << "\","
              << "\"taskSystem\":\"DisabledTaskSystem\","
              << "\"platform\":\"Headless\","
              << "\"render\":\"Null\""
              << "},"
              << "\"timing_ns\":{"
              << "\"p50\":" << p50 << ','
              << "\"p95\":" << p95 << ','
              << "\"p99\":" << p99 << ','
              << "\"max\":" << maxNs << ','
              << "\"mean\":" << meanNs << ','
              << "\"count\":" << frameNs.size() << ','
              << "\"wall\":" << wallNs
              << "},"
              << "\"counters\":{"
              << "\"frame_updates\":" << counters.frameUpdates << ','
              << "\"state_exits\":" << counters.stateExits << ','
              << "\"application_shutdowns\":" << counters.applicationShutdowns
              << "},"
              << "\"checksum\":\"" << checksum << "\","
              << "\"exit\":\"GameRequestedExitAfterCurrentFrame\","
              << "\"notes\":["
              << "\"shared_dev_or_ci_is_provisional_not_hard_gate\","
              << "\"tracy_disabled\","
              << "\"single_process_run\""
              << "]"
              << "}\n";
    (void)runResult;

    // Determinism smoke: expected counter relationship for this workload.
    if (counters.frameUpdates != totalFrames || counters.stateExits != 1 || counters.applicationShutdowns != 1) {
        std::cerr << "{\"status\":\"error\",\"schema\":" << kSchemaVersion
                  << ",\"message\":\"counter invariant failed\"}\n";
        return 1;
    }
    return 0;
}
