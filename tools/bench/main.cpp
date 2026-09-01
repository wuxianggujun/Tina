#include "UIBenchmarkWorkloads.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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
    std::string_view workload = kWorkloadId;
    bool help = false;
};

struct Counters final {
    u64 frameUpdates = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
};

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options, std::string& error)
{
    Tina::Core::ArgScanner scanner(argc, argv);
    while (scanner.next()) {
        if (scanner.flag("--help") || scanner.flag("-h")) {
            options.help = true;
            return true;
        }
        if (const auto value = scanner.value("--warmup")) {
            if (!Tina::Core::parseArgUnsigned(*value, options.warmUpFrames)) {
                error = "invalid --warmup value";
                return false;
            }
            continue;
        }
        if (const auto value = scanner.value("--samples")) {
            if (!Tina::Core::parseArgUnsigned(*value, options.measureFrames)
                || options.measureFrames == 0) {
                error = "invalid --samples value";
                return false;
            }
            continue;
        }
        if (const auto value = scanner.value("--seed")) {
            if (!Tina::Core::parseArgUnsigned(*value, options.seed)) {
                error = "invalid --seed value";
                return false;
            }
            continue;
        }
        if (const auto value = scanner.value("--workload")) {
            if (*value != kWorkloadId && !Tina::Bench::isUIBenchmarkWorkload(*value)) {
                error = "unknown workload (see --help for supported workload ids)";
                return false;
            }
            options.workload = *value;
            continue;
        }
        if (scanner.failed()) {
            error = "missing value for ";
            error.append(scanner.failedOption());
            return false;
        }
        error = "unknown argument: ";
        error.append(scanner.token());
        return false;
    }

    if (options.measureFrames == 0
        || options.measureFrames > static_cast<u64>((std::numeric_limits<std::size_t>::max)())
        || options.warmUpFrames > (std::numeric_limits<u64>::max)() - options.measureFrames) {
        error = "invalid frame count";
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
        << "  -h, --help\n";
    Tina::Bench::printUIBenchmarkHelp(std::cout);
    std::cout << "Note: shared CI/dev machines produce provisional conclusions only.\n";
}

} // namespace

int main(int argc, char** argv)
{
    Options options{};
    std::string parseError;
    if (!parseOptions(argc, argv, options, parseError)) {
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("status", "error");
        writer.member("schema", kSchemaVersion);
        writer.member("message", parseError);
        writer.endObject();
        std::cerr << '\n';
        return 2;
    }
    if (options.help) {
        printHelp();
        return 0;
    }
    if (Tina::Bench::isUIBenchmarkWorkload(options.workload)) {
        return Tina::Bench::runUIBenchmark(
            options.workload,
            Tina::Bench::UIBenchmarkOptions{
                .warmUpIterations = options.warmUpFrames,
                .measureIterations = options.measureFrames,
                .seed = options.seed,
            },
            std::cout,
            std::cerr);
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
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("status", "error");
        writer.member("schema", kSchemaVersion);
        writer.member("message", "EngineHost::Create failed");
        writer.endObject();
        std::cerr << '\n';
        return 1;
    }

    BenchGameApplication app{totalFrames, options.warmUpFrames, options.seed, counters, frameNs};
    const auto wallBegin = Tina::Core::SteadyMonotonicClock{}.now();
    auto runResult = (*hostResult)->run(app);
    const auto wallEnd = Tina::Core::SteadyMonotonicClock{}.now();
    if (!runResult) {
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("status", "error");
        writer.member("schema", kSchemaVersion);
        writer.member("message", "EngineHost::run failed");
        writer.endObject();
        std::cerr << '\n';
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

    {
        Tina::Core::JsonWriter writer(std::cout);
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("schema", kSchemaVersion);
        writer.member("schemaName", "tina_bench");
        writer.member("conclusion", "provisional");
        writer.beginObjectMember("workload");
        writer.member("id", kWorkloadId);
        writer.member("version", kWorkloadVersion);
        writer.member("seed", options.seed);
        writer.beginObjectMember("parameters");
        writer.member("warmup_frames", options.warmUpFrames);
        writer.member("measure_frames", options.measureFrames);
        writer.endObject();
        writer.endObject();
        writer.beginObjectMember("fingerprint");
        writer.member("buildType", kBuildType);
        writer.member("hostOs", kHostOs);
        writer.member("taskSystem", "DisabledTaskSystem");
        writer.member("platform", "Headless");
        writer.member("render", "Null");
        writer.endObject();
        writer.beginObjectMember("timing_ns");
        writer.member("p50", p50);
        writer.member("p95", p95);
        writer.member("p99", p99);
        writer.member("max", maxNs);
        writer.member("mean", meanNs);
        writer.member("count", frameNs.size());
        writer.member("wall", wallNs);
        writer.endObject();
        writer.beginObjectMember("counters");
        writer.member("frame_updates", counters.frameUpdates);
        writer.member("state_exits", counters.stateExits);
        writer.member("application_shutdowns", counters.applicationShutdowns);
        writer.endObject();
        writer.member("checksum", checksum);
        writer.member("exit", "GameRequestedExitAfterCurrentFrame");
        writer.beginArrayMember("notes");
        writer.element("shared_dev_or_ci_is_provisional_not_hard_gate");
        writer.element("tracy_disabled");
        writer.element("single_process_run");
        writer.endArray();
        writer.endObject();
    }
    std::cout << '\n';
    (void)runResult;

    // Determinism smoke: expected counter relationship for this workload.
    if (counters.frameUpdates != totalFrames || counters.stateExits != 1 || counters.applicationShutdowns != 1) {
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("status", "error");
        writer.member("schema", kSchemaVersion);
        writer.member("message", "counter invariant failed");
        writer.endObject();
        std::cerr << '\n';
        return 1;
    }
    return 0;
}
