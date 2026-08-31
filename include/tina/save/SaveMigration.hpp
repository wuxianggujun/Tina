#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/save/SaveTypes.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace Tina::Save {

inline constexpr Core::u32 DefaultSaveMigrationStepCapacity = 64;
inline constexpr Core::u32 MaxSaveMigrationStepCapacity = 1024;

struct SaveMigrationConfig final {
    Core::u64 maxPayloadBytes = DefaultMaxSavePayloadBytes;
    Core::u32 stepCapacity = DefaultSaveMigrationStepCapacity;
};

using SaveMigrationFunction = Core::MoveOnlyFunction<
    Core::Result<std::vector<std::byte>>(std::span<const std::byte>)>;

// Product-owned migration graph. Each source version has exactly one strictly
// increasing edge, making migration deterministic and downgrade-free.
class SaveMigrationPipeline final {
  public:
    SaveMigrationPipeline() = delete;
    ~SaveMigrationPipeline() noexcept;

    SaveMigrationPipeline(const SaveMigrationPipeline&) = delete;
    SaveMigrationPipeline& operator=(const SaveMigrationPipeline&) = delete;
    SaveMigrationPipeline(SaveMigrationPipeline&&) noexcept;
    SaveMigrationPipeline& operator=(SaveMigrationPipeline&&) noexcept;

    [[nodiscard]] static Core::Result<SaveMigrationPipeline> Create(
        SaveMigrationConfig config = {});

    [[nodiscard]] Core::Status addStep(
        Core::u32 fromVersion, Core::u32 toVersion, SaveMigrationFunction migration);

    [[nodiscard]] Core::Result<std::vector<std::byte>> migrate(
        Core::u32 sourceVersion,
        std::span<const std::byte> payload,
        Core::u32 targetVersion);

  private:
    class Impl;
    explicit SaveMigrationPipeline(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace Tina::Save
