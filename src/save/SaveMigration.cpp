#include <tina/save/SaveMigration.hpp>

#include <tina/save/SaveErrors.hpp>

#include <algorithm>
#include <new>
#include <string>
#include <utility>

namespace Tina::Save {

class SaveMigrationPipeline::Impl final {
  public:
    struct Step final {
        Core::u32 fromVersion = 0;
        Core::u32 toVersion = 0;
        SaveMigrationFunction migrate{};
    };

    explicit Impl(SaveMigrationConfig value) : config(value)
    {
        steps.reserve(value.stepCapacity);
    }

    SaveMigrationConfig config{};
    std::vector<Step> steps{};
};

SaveMigrationPipeline::SaveMigrationPipeline(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

SaveMigrationPipeline::~SaveMigrationPipeline() noexcept = default;
SaveMigrationPipeline::SaveMigrationPipeline(SaveMigrationPipeline&&) noexcept = default;
SaveMigrationPipeline& SaveMigrationPipeline::operator=(SaveMigrationPipeline&&) noexcept = default;

Core::Result<SaveMigrationPipeline> SaveMigrationPipeline::Create(SaveMigrationConfig config)
try
{
    if (config.maxPayloadBytes == 0 || config.maxPayloadBytes > MaxSavePayloadBytes ||
        config.stepCapacity == 0 || config.stepCapacity > MaxSaveMigrationStepCapacity)
    {
        return Core::failure(SaveErrorCode::InvalidConfiguration,
                             "save migration limits are zero or exceed hard limits");
    }
    return SaveMigrationPipeline{std::make_unique<Impl>(config)};
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "save migration pipeline allocation failed");
}

Core::Status SaveMigrationPipeline::addStep(
    Core::u32 fromVersion, Core::u32 toVersion, SaveMigrationFunction migration)
try
{
    if (fromVersion == 0 || toVersion == 0 || toVersion <= fromVersion || !migration)
    {
        return Core::failure(SaveErrorCode::InvalidMigrationVersion,
                             "migration step requires a callable and strictly increasing non-zero versions");
    }
    if (impl_->steps.size() >= impl_->config.stepCapacity)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "save migration step capacity exceeded");
    }
    const auto existing = std::ranges::find_if(
        impl_->steps, [fromVersion](const Impl::Step& step) { return step.fromVersion == fromVersion; });
    if (existing != impl_->steps.end())
    {
        return Core::failure(SaveErrorCode::DuplicateMigrationStep,
                             "a migration step already owns this source version");
    }
    impl_->steps.push_back(Impl::Step{
        .fromVersion = fromVersion,
        .toVersion = toVersion,
        .migrate = std::move(migration),
    });
    return Core::success();
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "save migration step allocation failed");
}

Core::Result<std::vector<std::byte>> SaveMigrationPipeline::migrate(
    Core::u32 sourceVersion,
    std::span<const std::byte> payload,
    Core::u32 targetVersion)
try
{
    if (sourceVersion == 0 || targetVersion == 0)
    {
        return Core::failure(SaveErrorCode::InvalidMigrationVersion,
                             "save migration versions must be non-zero");
    }
    if (sourceVersion > targetVersion)
    {
        return Core::failure(SaveErrorCode::InvalidMigrationVersion,
                             "save migration does not support downgrades");
    }
    if (payload.size() > impl_->config.maxPayloadBytes)
    {
        return Core::failure(SaveErrorCode::PayloadTooLarge,
                             "source save payload exceeds the migration limit");
    }

    std::vector<std::byte> current{payload.begin(), payload.end()};
    Core::u32 currentVersion = sourceVersion;
    while (currentVersion != targetVersion)
    {
        const auto step = std::ranges::find_if(
            impl_->steps,
            [currentVersion](const Impl::Step& candidate) {
                return candidate.fromVersion == currentVersion;
            });
        if (step == impl_->steps.end() || step->toVersion > targetVersion)
        {
            return Core::failure(SaveErrorCode::MigrationPathMissing,
                                 "save migration path has a missing version step");
        }

        auto migrated = step->migrate(current);
        if (!migrated)
        {
            std::string detail = std::to_string(step->fromVersion);
            detail.append(" -> ");
            detail.append(std::to_string(step->toVersion));
            return Core::failure(
                std::move(migrated.error()).withContext("SaveMigrationPipeline::migrate", detail));
        }
        if (migrated->size() > impl_->config.maxPayloadBytes)
        {
            return Core::failure(SaveErrorCode::PayloadTooLarge,
                                 "migration step produced a payload above the configured limit");
        }
        current = std::move(*migrated);
        currentVersion = step->toVersion;
    }
    return current;
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "save migration payload allocation failed");
}
catch (...)
{
    return Core::failure(SaveErrorCode::MigrationFailed,
                         "save migration callback threw an exception");
}

} // namespace Tina::Save
