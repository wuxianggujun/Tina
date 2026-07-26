#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/task/TaskErrors.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory_resource>
#include <new>
#include <thread>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

using TestSupport::TrackingMemoryResource;
using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::writeTextureMaterialPackage;

class ControlledTaskSystem final : public Task::ITaskSystem {
  public:
    [[nodiscard]] bool isIdle() const noexcept override
    {
        return m_io.empty();
    }

    [[nodiscard]] bool isStopping() const noexcept override
    {
        return m_stopping;
    }

    [[nodiscard]] Core::Status scheduleIo(Task::TaskCallable work) override
    {
        if (m_throwAllocationOnNextIo)
        {
            m_throwAllocationOnNextIo = false;
            throw std::bad_alloc{};
        }
        if (m_stopping)
        {
            return Core::failure(Task::TaskErrorCode::TaskSystemStopped, "controlled task system is stopping");
        }
        if (m_rejectIoCount != 0U)
        {
            --m_rejectIoCount;
            return Core::failure(Task::TaskErrorCode::QueueFull, "controlled IO queue is full");
        }
        m_io.push_back(std::move(work));
        return Core::success();
    }

    [[nodiscard]] Core::Status scheduleCpu(Task::TaskCallable work) override
    {
        static_cast<void>(work);
        return Core::failure(Task::TaskErrorCode::NotSupported, "controlled task system has no CPU domain");
    }

    [[nodiscard]] Core::Status postMain(Task::TaskCallable work) override
    {
        static_cast<void>(work);
        ++m_postMainCalls;
        return Core::failure(Task::TaskErrorCode::QueueFull, "controlled Main queue is full");
    }

    [[nodiscard]] Core::Result<Core::u32> pumpMain(Core::u32 budget) override
    {
        static_cast<void>(budget);
        return 0U;
    }

    void requestStop() noexcept override
    {
        m_stopping = true;
    }

    void shutdownAndJoin() noexcept override
    {
        m_stopping = true;
        m_io.clear();
    }

    [[nodiscard]] Core::Status shutdownAndJoinFor(Core::Duration deadline) noexcept override
    {
        if (!std::isfinite(deadline.count()) || deadline <= Core::Duration::zero())
        {
            return Core::failure(Task::TaskErrorCode::InvalidArgument,
                                 "shutdown deadline must be finite and greater than zero");
        }
        shutdownAndJoin();
        return Core::success();
    }

    void rejectNextIo(Core::u32 count = 1U) noexcept
    {
        m_rejectIoCount = count;
    }

    void throwAllocationOnNextIo() noexcept
    {
        m_throwAllocationOnNextIo = true;
    }

    [[nodiscard]] Core::usize queuedIo() const noexcept
    {
        return m_io.size();
    }

    [[nodiscard]] Core::u32 postMainCalls() const noexcept
    {
        return m_postMainCalls;
    }

    void runIoAt(Core::usize index)
    {
        ASSERT_LT(index, m_io.size());
        auto work = std::move(m_io[index]);
        m_io.erase(m_io.begin() + static_cast<std::ptrdiff_t>(index));
        ASSERT_TRUE(static_cast<bool>(work));
        work();
    }

  private:
    std::vector<Task::TaskCallable> m_io;
    Core::u32 m_rejectIoCount = 0;
    Core::u32 m_postMainCalls = 0;
    bool m_throwAllocationOnNextIo = false;
    bool m_stopping = false;
};

class OwnerMemoryResource final : public std::pmr::memory_resource {
  public:
    void setAllocationsAllowed(bool allowed) noexcept
    {
        m_allocationsAllowed = allowed;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (!m_allocationsAllowed)
        {
            throw std::bad_alloc{};
        }
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    bool m_allocationsAllowed = true;
};

[[nodiscard]] AssetSystemConfig asyncConfig(std::pmr::memory_resource& resource, Task::ITaskSystem& taskSystem,
                                            Core::usize queueCapacity = 8)
{
    return AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .memoryResource = &resource,
            },
        .queueCapacity = queueCapacity,
        .defaultPumpBudget = 4,
        .taskSystem = &taskSystem,
    };
}

[[nodiscard]] Core::Status bindPackage(AssetSystem& system, const TestSupport::TextureMaterialPackage& package,
                                       std::pmr::memory_resource& resource)
{
    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &resource,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .verifyContent = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(package.root), openConfig);
    if (!catalog)
    {
        return Core::failure(std::move(catalog.error()).withContext("AssetSystemAsyncPumpTests", "openCatalog"));
    }
    return system.bindCatalog(toUtf8(package.root), std::move(*catalog));
}

TEST(AssetSystemAsyncPumpTests, RequestIoPumpMakesReady)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_pump_ok");

    auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 16,
        .mainQueueCapacity = 16,
    });
    ASSERT_TRUE(taskSystem.has_value()) << taskSystem.error().message;

    auto system = AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .memoryResource = &resource,
            },
        .queueCapacity = 8,
        .defaultPumpBudget = 4,
        .taskSystem = taskSystem->get(),
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &resource,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .verifyContent = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(package.root), openConfig);
    ASSERT_TRUE(catalog.has_value());
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*catalog)).has_value());

    auto requested = system->request(std::array{package.materialId});
    ASSERT_TRUE(requested.has_value()) << requested.error().message;
    EXPECT_EQ(system->state((*requested)[0]), AssetLogicalState::Queued);

    bool ready = false;
    for (int frame = 0; frame < 200 && !ready; ++frame)
    {
        auto stats = system->pump(4);
        ASSERT_TRUE(stats.has_value()) << stats.error().message;
        if (system->state((*requested)[0]) == AssetLogicalState::ReadyCpu)
        {
            ready = true;
            break;
        }
        if (stats->inFlight > 0 || stats->remaining > 0 || stats->dispatchedIo > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    EXPECT_TRUE(ready);
    EXPECT_EQ(system->state((*requested)[0]), AssetLogicalState::ReadyCpu);
    auto lease = system->acquire((*requested)[0]);
    ASSERT_TRUE(lease.has_value());
    EXPECT_EQ(lease->assetId(), package.materialId);

    (*taskSystem)->shutdownAndJoin();
    removePackage(package);
}

TEST(AssetSystemAsyncPumpTests, IoQueueFullPreservesQueuedRequestForStableRetry)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_queue_retry");
    ControlledTaskSystem taskSystem;
    auto system = AssetSystem::Create(asyncConfig(resource, taskSystem, 2));
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(bindPackage(*system, package, resource).has_value());

    auto handle = system->requestOne(package.textureId);
    ASSERT_TRUE(handle.has_value()) << handle.error().message;
    taskSystem.rejectNextIo();

    auto rejected = system->pump(1);
    ASSERT_TRUE(rejected.has_value()) << rejected.error().message;
    EXPECT_EQ(rejected->dispatchedIo, 0U);
    EXPECT_EQ(rejected->becameFailed, 0U);
    EXPECT_EQ(system->pendingCount(), 1U);
    EXPECT_EQ(system->inFlightCount(), 0U);
    EXPECT_EQ(system->state(*handle), AssetLogicalState::Queued);

    auto retried = system->pump(1);
    ASSERT_TRUE(retried.has_value()) << retried.error().message;
    EXPECT_EQ(retried->dispatchedIo, 1U);
    EXPECT_EQ(system->state(*handle), AssetLogicalState::Loading);
    ASSERT_EQ(taskSystem.queuedIo(), 1U);

    taskSystem.runIoAt(0);
    auto committed = system->pump(1);
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_EQ(committed->mainCompletions, 1U);
    EXPECT_EQ(committed->becameReady, 1U);
    EXPECT_EQ(system->state(*handle), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(taskSystem.postMainCalls(), 0U);

    removePackage(package);
}

TEST(AssetSystemAsyncPumpTests, ScheduleAllocationExceptionRollsBackForRetry)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_schedule_exception");
    ControlledTaskSystem taskSystem;
    auto system = AssetSystem::Create(asyncConfig(resource, taskSystem, 2));
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(bindPackage(*system, package, resource).has_value());

    auto handle = system->requestOne(package.textureId);
    ASSERT_TRUE(handle.has_value()) << handle.error().message;
    taskSystem.throwAllocationOnNextIo();

    auto rejected = system->pump(1);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, AssetErrorCode::AllocationFailed);
    EXPECT_EQ(system->pendingCount(), 1U);
    EXPECT_EQ(system->inFlightCount(), 0U);
    EXPECT_EQ(system->state(*handle), AssetLogicalState::Queued);
    EXPECT_EQ(taskSystem.queuedIo(), 0U);

    auto retried = system->pump(1);
    ASSERT_TRUE(retried.has_value()) << retried.error().message;
    EXPECT_EQ(retried->dispatchedIo, 1U);
    EXPECT_EQ(system->inFlightCount(), 1U);
    ASSERT_EQ(taskSystem.queuedIo(), 1U);

    taskSystem.runIoAt(0);
    auto committed = system->pump(1);
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_EQ(committed->mainCompletions, 1U);
    EXPECT_EQ(system->inFlightCount(), 0U);
    EXPECT_EQ(system->state(*handle), AssetLogicalState::ReadyCpu);

    removePackage(package);
}

TEST(AssetSystemAsyncPumpTests, WorkerReadDoesNotUseOwnerMemoryResource)
{
    OwnerMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_owner_memory");
    ControlledTaskSystem taskSystem;
    auto system = AssetSystem::Create(asyncConfig(resource, taskSystem, 2));
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(bindPackage(*system, package, resource).has_value());

    auto handle = system->requestOne(package.textureId);
    ASSERT_TRUE(handle.has_value()) << handle.error().message;
    ASSERT_TRUE(system->pump(1).has_value());
    ASSERT_EQ(taskSystem.queuedIo(), 1U);

    resource.setAllocationsAllowed(false);
    taskSystem.runIoAt(0);
    resource.setAllocationsAllowed(true);

    auto committed = system->pump(1);
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_EQ(committed->mainCompletions, 1U);
    EXPECT_EQ(committed->becameReady, 1U);
    EXPECT_EQ(system->state(*handle), AssetLogicalState::ReadyCpu);

    removePackage(package);
}

TEST(AssetSystemAsyncPumpTests, BudgetOneCommitsOnlyOneCompletedRequestPerPump)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_completion_budget");
    ControlledTaskSystem taskSystem;
    auto system = AssetSystem::Create(asyncConfig(resource, taskSystem, 4));
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(bindPackage(*system, package, resource).has_value());

    auto handles = system->request(std::array{package.materialId});
    ASSERT_TRUE(handles.has_value()) << handles.error().message;
    ASSERT_EQ(system->pendingCount(), 2U);

    auto dispatched = system->pump(2);
    ASSERT_TRUE(dispatched.has_value()) << dispatched.error().message;
    EXPECT_EQ(dispatched->processed, 2U);
    EXPECT_EQ(dispatched->dispatchedIo, 2U);
    EXPECT_EQ(dispatched->mainCompletions, 0U);
    ASSERT_EQ(taskSystem.queuedIo(), 2U);

    taskSystem.runIoAt(0);
    taskSystem.runIoAt(0);

    auto firstCommit = system->pump(1);
    ASSERT_TRUE(firstCommit.has_value()) << firstCommit.error().message;
    EXPECT_EQ(firstCommit->processed, 0U);
    EXPECT_EQ(firstCommit->dispatchedIo, 0U);
    EXPECT_EQ(firstCommit->mainCompletions, 1U);
    EXPECT_EQ(system->inFlightCount(), 1U);

    auto secondCommit = system->pump(1);
    ASSERT_TRUE(secondCommit.has_value()) << secondCommit.error().message;
    EXPECT_EQ(secondCommit->processed, 0U);
    EXPECT_EQ(secondCommit->dispatchedIo, 0U);
    EXPECT_EQ(secondCommit->mainCompletions, 1U);
    EXPECT_EQ(system->inFlightCount(), 0U);
    EXPECT_EQ(system->state((*handles)[0]), AssetLogicalState::ReadyCpu);

    removePackage(package);
}

TEST(AssetSystemAsyncPumpTests, CompletionConsumesBudgetBeforePendingDispatch)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_shared_pump_budget");
    ControlledTaskSystem taskSystem;
    auto system = AssetSystem::Create(asyncConfig(resource, taskSystem, 4));
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(bindPackage(*system, package, resource).has_value());

    auto handles = system->request(std::array{package.materialId});
    ASSERT_TRUE(handles.has_value()) << handles.error().message;
    ASSERT_EQ(system->pendingCount(), 2U);

    auto firstDispatch = system->pump(1);
    ASSERT_TRUE(firstDispatch.has_value()) << firstDispatch.error().message;
    EXPECT_EQ(firstDispatch->processed, 1U);
    EXPECT_EQ(firstDispatch->dispatchedIo, 1U);
    EXPECT_EQ(firstDispatch->mainCompletions, 0U);
    EXPECT_EQ(firstDispatch->remaining, 1U);
    ASSERT_EQ(taskSystem.queuedIo(), 1U);

    taskSystem.runIoAt(0);
    auto commitOnly = system->pump(1);
    ASSERT_TRUE(commitOnly.has_value()) << commitOnly.error().message;
    EXPECT_EQ(commitOnly->processed, 0U);
    EXPECT_EQ(commitOnly->dispatchedIo, 0U);
    EXPECT_EQ(commitOnly->mainCompletions, 1U);
    EXPECT_EQ(commitOnly->remaining, 1U);
    EXPECT_EQ(taskSystem.queuedIo(), 0U);

    auto secondDispatch = system->pump(1);
    ASSERT_TRUE(secondDispatch.has_value()) << secondDispatch.error().message;
    EXPECT_EQ(secondDispatch->processed, 1U);
    EXPECT_EQ(secondDispatch->dispatchedIo, 1U);
    EXPECT_EQ(secondDispatch->mainCompletions, 0U);
    EXPECT_EQ(secondDispatch->remaining, 0U);
    EXPECT_EQ(taskSystem.queuedIo(), 1U);

    removePackage(package);
}

TEST(AssetSystemAsyncPumpTests, CancelLoadingThenReentryPublishesOnlyNewGenerationInDispatchOrder)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_cancel_reentry");
    ControlledTaskSystem taskSystem;
    auto system = AssetSystem::Create(asyncConfig(resource, taskSystem, 4));
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(bindPackage(*system, package, resource).has_value());

    auto oldHandle = system->requestOne(package.textureId);
    ASSERT_TRUE(oldHandle.has_value());
    ASSERT_TRUE(system->pump(1).has_value());
    ASSERT_EQ(taskSystem.queuedIo(), 1U);
    ASSERT_EQ(system->state(*oldHandle), AssetLogicalState::Loading);
    EXPECT_EQ(system->inFlightCount(), 1U);

    ASSERT_TRUE(system->unload(*oldHandle).has_value());
    EXPECT_EQ(system->find(package.textureId), std::nullopt);
    EXPECT_EQ(system->state(*oldHandle), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->inFlightCount(), 1U);

    auto newHandle = system->requestOne(package.textureId);
    ASSERT_TRUE(newHandle.has_value());
    EXPECT_NE(*newHandle, *oldHandle);
    ASSERT_TRUE(system->pump(1).has_value());
    ASSERT_EQ(taskSystem.queuedIo(), 2U);
    EXPECT_EQ(system->inFlightCount(), 2U);

    // Finish the newer IO first. Stable prefix commit keeps it Loading until the
    // canceled older dispatch is observed and discarded.
    taskSystem.runIoAt(1);
    auto outOfOrder = system->pump(1);
    ASSERT_TRUE(outOfOrder.has_value());
    EXPECT_EQ(outOfOrder->mainCompletions, 0U);
    EXPECT_EQ(system->state(*newHandle), AssetLogicalState::Loading);
    EXPECT_EQ(system->inFlightCount(), 2U);

    taskSystem.runIoAt(0);
    auto committed = system->pump(0);
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_EQ(committed->mainCompletions, 2U);
    EXPECT_EQ(committed->becameReady, 1U);
    EXPECT_EQ(system->state(*oldHandle), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->state(*newHandle), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(system->find(package.textureId), newHandle);
    EXPECT_EQ(system->inFlightCount(), 0U);
    EXPECT_EQ(taskSystem.postMainCalls(), 0U);

    removePackage(package);
}

TEST(AssetSystemAsyncPumpTests, ActiveReadSurvivesAssetSystemMove)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_move");
    ControlledTaskSystem taskSystem;
    auto source = AssetSystem::Create(asyncConfig(resource, taskSystem, 2));
    ASSERT_TRUE(source.has_value());
    ASSERT_TRUE(bindPackage(*source, package, resource).has_value());

    auto handle = source->requestOne(package.textureId);
    ASSERT_TRUE(handle.has_value());
    ASSERT_TRUE(source->pump(1).has_value());
    ASSERT_EQ(taskSystem.queuedIo(), 1U);

    AssetSystem moved{std::move(*source)};
    taskSystem.runIoAt(0);
    auto committed = moved.pump(1);
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_EQ(moved.state(*handle), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(moved.inFlightCount(), 0U);

    removePackage(package);
}

TEST(AssetSystemAsyncPumpTests, ActiveReadDoesNotReferenceDestroyedAssetSystem)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_destroy");
    ControlledTaskSystem taskSystem;
    {
        auto system = AssetSystem::Create(asyncConfig(resource, taskSystem, 2));
        ASSERT_TRUE(system.has_value());
        ASSERT_TRUE(bindPackage(*system, package, resource).has_value());
        ASSERT_TRUE(system->requestOne(package.textureId).has_value());
        ASSERT_TRUE(system->pump(1).has_value());
        ASSERT_EQ(taskSystem.queuedIo(), 1U);
    }

    // The worker owns only immutable path/config plus its request state. Running it
    // after AssetSystem destruction must not post through or dereference the old owner.
    taskSystem.runIoAt(0);
    EXPECT_EQ(taskSystem.postMainCalls(), 0U);
    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
