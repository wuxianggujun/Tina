#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <algorithm>
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
using Tina::Physics2D::PhysicsShape2DDesc;
using Tina::Physics2D::PhysicsShapeKind2D;
using Tina::Physics2D::PhysicsWorld2D;
using Tina::Physics2D::PhysicsWorld2DConfig;

struct Options final {
    std::uint32_t dynamicBodies = 64;
    std::uint32_t warmUpSteps = 120;
    std::uint32_t measureSteps = 600;
    std::uint32_t queryRays = 0;
    bool help = false;
};

// Every failure path prints the same three keys, so the shape lives here once rather than in
// five places.
void writeError(std::string_view message)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", "tina_physics2d_bench");
    writer.member("message", message);
    writer.endObject();
    std::cerr << '\n';
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
            if (!Tina::Core::parseArgUnsigned(argument.substr(bodiesPrefix.size()), options.dynamicBodies)
                || options.dynamicBodies == 0) {
                error = "invalid --bodies value";
                return false;
            }
            continue;
        }
        if (argument.starts_with(warmPrefix)) {
            if (!Tina::Core::parseArgUnsigned(argument.substr(warmPrefix.size()), options.warmUpSteps)) {
                error = "invalid --warmup value";
                return false;
            }
            continue;
        }
        if (argument.starts_with(measurePrefix)) {
            if (!Tina::Core::parseArgUnsigned(argument.substr(measurePrefix.size()), options.measureSteps)
                || options.measureSteps == 0) {
                error = "invalid --steps value";
                return false;
            }
            continue;
        }
        if (argument.starts_with(raysPrefix)) {
            if (!Tina::Core::parseArgUnsigned(argument.substr(raysPrefix.size()), options.queryRays)) {
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
    const auto createBodyWithShape = [&world](const PhysicsBody2DDesc& body,
                                               const PhysicsShape2DDesc& shape) {
        auto bodyId = world.createBody(body);
        if (!bodyId) {
            return false;
        }
        auto shapeId = world.createShape(*bodyId, shape);
        if (!shapeId) {
            (void)world.destroyBody(*bodyId);
            return false;
        }
        return true;
    };

    PhysicsBody2DDesc groundBody;
    groundBody.type = PhysicsBodyType2D::Static;
    groundBody.positionMeters = {0.0F, -1.0F};
    PhysicsShape2DDesc groundShape;
    groundShape.kind = PhysicsShapeKind2D::Box;
    groundShape.halfExtentsMeters = {20.0F, 0.5F};
    groundShape.density = 0.0F;
    groundShape.enableContactEvents = false;
    if (!createBodyWithShape(groundBody, groundShape)) {
        return false;
    }

    PhysicsShape2DDesc boxShape;
    boxShape.kind = PhysicsShapeKind2D::Box;
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
        if (!createBodyWithShape(body, boxShape)) {
            return false;
        }
    }
    return true;
}

} // namespace

int runPhysics2dBench(int argc, char** argv)
{
    Options options;
    std::string parseError;
    if (!parseOptions(argc, argv, options, parseError)) {
        writeError(parseError);
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
        writeError(worldResult.error().message);
        return 1;
    }
    PhysicsWorld2D world = std::move(*worldResult);
    if (!buildStackScene(world, options.dynamicBodies)) {
        writeError("failed to build stack scene");
        return 1;
    }

    Tina::Core::SteadyMonotonicClock clock;
    for (std::uint32_t step = 0; step < options.warmUpSteps; ++step) {
        if (!world.step()) {
            writeError("warm-up step failed");
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
            writeError("measure step failed");
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
        writeError("shutdown failed");
        return 1;
    }

    {
        Tina::Core::JsonWriter writer(std::cout);
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("sample", "tina_physics2d_bench");
        writer.member("workload", "stack_dynamic");
        writer.member("bodies", options.dynamicBodies);
        writer.member("warmup_steps", options.warmUpSteps);
        writer.member("measured_steps", options.measureSteps);
        writer.member("rays_per_step", options.queryRays);
        writer.beginObjectMember("step_ns");
        writer.member("p50", p50);
        writer.member("p95", p95);
        writer.member("p99", p99);
        writer.member("max", maxNs);
        writer.member("mean", meanNs);
        writer.endObject();
        writer.member("query_ns_total", queryNsTotal);
        writer.member("query_hits_total", queryHitTotal);
        writer.beginObjectMember("world");
        writer.member("body_count", statistics.bodyCount);
        writer.member("shape_count", statistics.shapeCount);
        writer.member("completed_steps", statistics.completedStepCount);
        writer.endObject();
        writer.member("note", "single-thread baseline only; not ADR-0018 tina_bench schema");
        writer.endObject();
    }
    std::cout << '\n';
    return 0;
}
