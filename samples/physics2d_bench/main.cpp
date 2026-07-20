#include <tina/core/time/MonotonicClock.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Tina::Physics2D::PhysicsBody2DDesc;
using Tina::Physics2D::PhysicsBodyType2D;
using Tina::Physics2D::PhysicsBoxShape2DDesc;
using Tina::Physics2D::PhysicsWorld2D;
using Tina::Physics2D::PhysicsWorld2DConfig;

struct Options final {
    std::uint32_t dynamicBodies = 64;
    std::uint32_t warmUpSteps = 120;
    std::uint32_t measureSteps = 600;
    std::uint32_t queryRays = 0;
    bool help = false;
};

[[nodiscard]] bool parseU32(std::string_view text, std::uint32_t& out) noexcept
{
    std::uint32_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    out = value;
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
        constexpr std::string_view bodiesPrefix = "--bodies=";
        constexpr std::string_view warmPrefix = "--warmup=";
        constexpr std::string_view measurePrefix = "--steps=";
        constexpr std::string_view raysPrefix = "--rays=";
        if (argument.starts_with(bodiesPrefix)) {
            if (!parseU32(argument.substr(bodiesPrefix.size()), options.dynamicBodies)
                || options.dynamicBodies == 0) {
                error = "invalid --bodies value";
                return false;
            }
            continue;
        }
        if (argument.starts_with(warmPrefix)) {
            if (!parseU32(argument.substr(warmPrefix.size()), options.warmUpSteps)) {
                error = "invalid --warmup value";
                return false;
            }
            continue;
        }
        if (argument.starts_with(measurePrefix)) {
            if (!parseU32(argument.substr(measurePrefix.size()), options.measureSteps)
                || options.measureSteps == 0) {
                error = "invalid --steps value";
                return false;
            }
            continue;
        }
        if (argument.starts_with(raysPrefix)) {
            if (!parseU32(argument.substr(raysPrefix.size()), options.queryRays)) {
                error = "invalid --rays value";
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

[[nodiscard]] std::uint64_t nearestRankNs(
    std::vector<std::uint64_t>& samples,
    double quantile) noexcept
{
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const double rank = quantile * static_cast<double>(samples.size() - 1);
    const std::size_t index = static_cast<std::size_t>(std::llround(rank));
    return samples[(std::min)(index, samples.size() - 1)];
}

[[nodiscard]] PhysicsWorld2DConfig makeConfig(std::uint32_t dynamicBodies) noexcept
{
    PhysicsWorld2DConfig config;
    const std::size_t bodyCount = static_cast<std::size_t>(dynamicBodies) + 1U;
    config.bodyCapacity = bodyCount;
    config.shapeCapacity = bodyCount;
    config.contactBeginCapacity = bodyCount * 4U;
    config.contactEndCapacity = bodyCount * 4U;
    config.contactHitCapacity = 16U;
    config.commandCapacity = 16U;
    config.gravityMetersPerSecondSquared = {0.0F, -9.8F};
    config.fixedDeltaSeconds = 1.0F / 60.0F;
    config.solverSubStepCount = 4;
    return config;
}

[[nodiscard]] bool buildStackScene(PhysicsWorld2D& world, std::uint32_t dynamicBodies)
{
    PhysicsBody2DDesc groundBody;
    groundBody.type = PhysicsBodyType2D::Static;
    groundBody.positionMeters = {0.0F, -1.0F};
    PhysicsBoxShape2DDesc groundShape;
    groundShape.halfExtentsMeters = {20.0F, 0.5F};
    groundShape.density = 0.0F;
    groundShape.enableContactEvents = false;
    if (!world.createBoxBody(groundBody, groundShape)) {
        return false;
    }

    PhysicsBoxShape2DDesc boxShape;
    boxShape.halfExtentsMeters = {0.4F, 0.4F};
    boxShape.density = 1.0F;
    boxShape.friction = 0.4F;
    boxShape.enableContactEvents = false;

    for (std::uint32_t index = 0; index < dynamicBodies; ++index) {
        PhysicsBody2DDesc body;
        body.type = PhysicsBodyType2D::Dynamic;
        const float column = static_cast<float>(index % 8);
        const float row = static_cast<float>(index / 8);
        body.positionMeters = {
            -3.5F + column * 1.0F,
            1.0F + row * 1.0F};
        body.initiallyAwake = true;
        if (!world.createBoxBody(body, boxShape)) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    std::string parseError;
    if (!parseOptions(argc, argv, options, parseError)) {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_physics2d_bench\",\"message\":\""
                  << parseError << "\"}\n";
        return 2;
    }
    if (options.help) {
        std::cout
            << "usage: tina_physics2d_bench [--bodies=N] [--warmup=N] [--steps=N] [--rays=N]\n"
            << "  Single-thread Physics2D step baseline (M11-A4). Not full tina_bench schema.\n";
        return 0;
    }

    auto worldResult = PhysicsWorld2D::Create(makeConfig(options.dynamicBodies));
    if (!worldResult) {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_physics2d_bench\",\"message\":\""
                  << worldResult.error().message << "\"}\n";
        return 1;
    }
    PhysicsWorld2D world = std::move(*worldResult);
    if (!buildStackScene(world, options.dynamicBodies)) {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_physics2d_bench\","
                     "\"message\":\"failed to build stack scene\"}\n";
        return 1;
    }

    Tina::Core::SteadyMonotonicClock clock;
    for (std::uint32_t step = 0; step < options.warmUpSteps; ++step) {
        if (!world.step()) {
            std::cerr << "{\"status\":\"error\",\"sample\":\"tina_physics2d_bench\","
                         "\"message\":\"warm-up step failed\"}\n";
            return 1;
        }
    }

    std::vector<std::uint64_t> stepNs;
    stepNs.reserve(options.measureSteps);
    std::uint64_t queryNsTotal = 0;
    std::uint64_t queryHitTotal = 0;

    Tina::Physics2D::PhysicsCastHit2D rayHits[8]{};
    for (std::uint32_t step = 0; step < options.measureSteps; ++step) {
        const auto begin = clock.now();
        if (!world.step()) {
            std::cerr << "{\"status\":\"error\",\"sample\":\"tina_physics2d_bench\","
                         "\"message\":\"measure step failed\"}\n";
            return 1;
        }
        const auto end = clock.now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);
        stepNs.push_back(static_cast<std::uint64_t>((std::max)(elapsed.count(), std::int64_t{0})));

        if (options.queryRays > 0) {
            const auto queryBegin = clock.now();
            for (std::uint32_t ray = 0; ray < options.queryRays; ++ray) {
                Tina::Physics2D::PhysicsRayCast2D cast{
                    {-5.0F, 0.5F + static_cast<float>(ray) * 0.1F},
                    {12.0F, 0.0F}};
                auto hits = world.castRay(cast, {}, rayHits);
                if (hits) {
                    queryHitTotal += static_cast<std::uint64_t>(hits->totalFound);
                }
            }
            const auto queryEnd = clock.now();
            queryNsTotal += static_cast<std::uint64_t>((std::max)(
                std::chrono::duration_cast<std::chrono::nanoseconds>(queryEnd - queryBegin).count(),
                std::int64_t{0}));
        }
    }

    const std::uint64_t p50 = nearestRankNs(stepNs, 0.50);
    const std::uint64_t p95 = nearestRankNs(stepNs, 0.95);
    const std::uint64_t p99 = nearestRankNs(stepNs, 0.99);
    std::uint64_t maxNs = 0;
    std::uint64_t sumNs = 0;
    for (const std::uint64_t sample : stepNs) {
        sumNs += sample;
        maxNs = (std::max)(maxNs, sample);
    }
    const double meanNs =
        stepNs.empty() ? 0.0 : static_cast<double>(sumNs) / static_cast<double>(stepNs.size());

    const auto statistics = world.stats();
    if (!world.shutdown()) {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_physics2d_bench\","
                     "\"message\":\"shutdown failed\"}\n";
        return 1;
    }

    std::cout
        << "{\"status\":\"ok\",\"sample\":\"tina_physics2d_bench\","
        << "\"workload\":\"stack_dynamic\","
        << "\"bodies\":" << options.dynamicBodies << ','
        << "\"warmup_steps\":" << options.warmUpSteps << ','
        << "\"measured_steps\":" << options.measureSteps << ','
        << "\"rays_per_step\":" << options.queryRays << ','
        << "\"step_ns\":{"
        << "\"p50\":" << p50 << ','
        << "\"p95\":" << p95 << ','
        << "\"p99\":" << p99 << ','
        << "\"max\":" << maxNs << ','
        << "\"mean\":" << meanNs
        << "},"
        << "\"query_ns_total\":" << queryNsTotal << ','
        << "\"query_hits_total\":" << queryHitTotal << ','
        << "\"world\":{"
        << "\"body_count\":" << statistics.bodyCount << ','
        << "\"shape_count\":" << statistics.shapeCount << ','
        << "\"completed_steps\":" << statistics.completedStepCount
        << "},"
        << "\"note\":\"single-thread baseline only; not ADR-0018 tina_bench schema\""
        << "}\n";
    return 0;
}
