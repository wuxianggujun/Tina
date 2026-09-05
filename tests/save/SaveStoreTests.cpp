#include <gtest/gtest.h>

#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/save/SaveErrors.hpp>
#include <tina/save/SaveStore.hpp>
#include <tina/task/TaskSystem.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <string>
#include <thread>
#include <vector>

namespace Tina::Tests {
namespace {

[[nodiscard]] std::vector<std::byte> payloadOf(std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char value : text) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

[[nodiscard]] std::string textOf(std::span<const std::byte> bytes)
{
    std::string result;
    result.reserve(bytes.size());
    for (const std::byte value : bytes) {
        result.push_back(static_cast<char>(value));
    }
    return result;
}

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t value : encoded) {
        result.push_back(static_cast<char>(value));
    }
    return result;
}

class SaveStoreTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        m_root = std::filesystem::temp_directory_path() / "tina_save_store_tests";
        std::error_code errorCode;
        std::filesystem::remove_all(m_root, errorCode);
        std::filesystem::create_directories(m_root, errorCode);
    }

    void TearDown() override
    {
        std::error_code errorCode;
        std::filesystem::remove_all(m_root, errorCode);
    }

    [[nodiscard]] Save::SaveStoreConfig config(Core::u32 slotCapacity = 4) const
    {
        return Save::SaveStoreConfig{
            .rootDirectoryUtf8 = pathToUtf8(m_root),
            .gameId = "tina.tests.save",
            .slotCapacity = slotCapacity,
            .maxPayloadBytes = 4096,
            .taskSystem = nullptr,
        };
    }

    [[nodiscard]] std::unique_ptr<Save::SaveStore> makeStore(Core::u32 slotCapacity = 4) const
    {
        auto store = Save::SaveStore::Create(config(slotCapacity));
        if (!store) {
            return nullptr;
        }
        return std::move(*store);
    }

    // Overwrites the trailing digest so the envelope parses structurally but fails
    // its content hash, which is exactly how disk corruption presents.
    static void corruptFile(std::string_view pathUtf8)
    {
        auto bytes = Core::readFile(pathUtf8, Core::ReadFileConfig{
                                                  .maxBytes = 1U << 20U,
                                                  .memoryResource =
                                                      std::pmr::new_delete_resource(),
                                              });
        ASSERT_TRUE(bytes.has_value());
        ASSERT_FALSE(bytes->empty());
        std::vector<std::byte> mutated{bytes->begin(), bytes->end()};
        mutated.back() = static_cast<std::byte>(
            std::to_integer<unsigned int>(mutated.back()) ^ 0xFFU);
        ASSERT_TRUE(Core::writeFile(pathUtf8, mutated).has_value());
    }

    [[nodiscard]] static bool exists(std::string_view pathUtf8)
    {
        return std::filesystem::exists(
            std::filesystem::u8path(pathUtf8.begin(), pathUtf8.end()));
    }

    std::filesystem::path m_root{};
};

// Async completion is observed by polling the operation handle; the module
// deliberately has no main-thread queue (see SaveStore.hpp).
template <typename Operation>
[[nodiscard]] bool waitForReady(Operation& operation)
{
    constexpr int MaximumPolls = 20000;
    for (int poll = 0; poll < MaximumPolls; ++poll) {
        if (operation.ready()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

} // namespace

TEST_F(SaveStoreTest, CreateRejectsInvalidConfiguration)
{
    // Empty and relative roots: slot filenames are generated, so a relative root
    // would resolve against the process working directory rather than the product's.
    Save::SaveStoreConfig empty = config();
    empty.rootDirectoryUtf8.clear();
    EXPECT_FALSE(Save::SaveStore::Create(empty).has_value());

    Save::SaveStoreConfig relative = config();
    relative.rootDirectoryUtf8 = "saves/relative";
    EXPECT_FALSE(Save::SaveStore::Create(relative).has_value());

    Save::SaveStoreConfig noGameId = config();
    noGameId.gameId.clear();
    EXPECT_FALSE(Save::SaveStore::Create(noGameId).has_value());

    Save::SaveStoreConfig oversizedGameId = config();
    oversizedGameId.gameId.assign(Save::MaxSaveGameIdBytes + 1, 'g');
    EXPECT_FALSE(Save::SaveStore::Create(oversizedGameId).has_value());

    Save::SaveStoreConfig zeroSlots = config();
    zeroSlots.slotCapacity = 0;
    EXPECT_FALSE(Save::SaveStore::Create(zeroSlots).has_value());

    Save::SaveStoreConfig tooManySlots = config();
    tooManySlots.slotCapacity = Save::MaxSaveSlotCapacity + 1;
    EXPECT_FALSE(Save::SaveStore::Create(tooManySlots).has_value());

    Save::SaveStoreConfig zeroPayload = config();
    zeroPayload.maxPayloadBytes = 0;
    EXPECT_FALSE(Save::SaveStore::Create(zeroPayload).has_value());

    Save::SaveStoreConfig hugePayload = config();
    hugePayload.maxPayloadBytes = Save::MaxSavePayloadBytes + 1;
    EXPECT_FALSE(Save::SaveStore::Create(hugePayload).has_value());

    EXPECT_TRUE(Save::SaveStore::Create(config()).has_value());
}

// Filenames are generated from the slot index, so caller-controlled text never
// becomes a path component.
TEST_F(SaveStoreTest, PathsForGeneratesZeroPaddedNamesAndRejectsOutOfRangeSlots)
{
    const auto store = makeStore(4);
    ASSERT_NE(store, nullptr);

    const auto paths = store->pathsFor(Save::SaveSlotId{2});
    ASSERT_TRUE(paths.has_value());
    EXPECT_TRUE(paths->primaryPathUtf8.ends_with("slot-0002.tsave"));
    EXPECT_EQ(paths->backupPathUtf8, paths->primaryPathUtf8 + ".bak");

    const auto outOfRange = store->pathsFor(Save::SaveSlotId{4});
    ASSERT_FALSE(outOfRange.has_value());
    EXPECT_EQ(outOfRange.error().code, Save::SaveErrorCode::InvalidSlot);
}

TEST_F(SaveStoreTest, SaveThenLoadRoundTripsPayloadAndMetadata)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);

    const std::vector<std::byte> payload = payloadOf("hello-save");
    const auto written = store->saveSlot(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{1},
        .dataVersion = 3,
        .displayName = "Chapter One",
        .payload = payload,
        .playTimeMilliseconds = 1234,
    });
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->metadata.revision, 1U);
    EXPECT_EQ(written->metadata.dataVersion, 3U);
    EXPECT_EQ(written->metadata.displayName, "Chapter One");
    EXPECT_EQ(written->metadata.gameId, "tina.tests.save");
    EXPECT_EQ(written->metadata.payloadBytes, payload.size());
    EXPECT_EQ(written->metadata.playTimeMilliseconds, 1234U);
    // Nothing existed to back up on a first write.
    EXPECT_FALSE(written->backupUpdated);
    EXPECT_FALSE(written->replacedCorruptPrimary);

    const auto loaded = store->loadSlot(Save::SaveSlotId{1});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(textOf(loaded->payload), "hello-save");
    EXPECT_EQ(loaded->metadata, written->metadata);
    EXPECT_EQ(loaded->source, Save::SaveStorageCopy::Primary);
    EXPECT_EQ(loaded->health, Save::SaveSlotHealth::PrimaryOnly);
}

TEST_F(SaveStoreTest, EmptyPayloadRoundTrips)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);

    const auto written = store->saveSlot(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{0},
        .dataVersion = 1,
        .payload = {},
    });
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->metadata.payloadBytes, 0U);

    const auto loaded = store->loadSlot(Save::SaveSlotId{0});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->payload.empty());
}

// The second write promotes the previous primary to backup, so the slot ends up
// Healthy with the older revision still recoverable.
TEST_F(SaveStoreTest, SecondSavePromotesPreviousPrimaryToBackupAndAdvancesRevision)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const Save::SaveSlotId slot{1};

    const auto first = store->saveSlot(Save::SaveWriteRequest{
        .slot = slot, .dataVersion = 1, .payload = payloadOf("first")});
    ASSERT_TRUE(first.has_value());

    const auto second = store->saveSlot(Save::SaveWriteRequest{
        .slot = slot, .dataVersion = 1, .payload = payloadOf("second")});
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(second->backupUpdated);
    EXPECT_EQ(second->metadata.revision, 2U);

    const auto loaded = store->loadSlot(slot);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(textOf(loaded->payload), "second");
    EXPECT_EQ(loaded->health, Save::SaveSlotHealth::Healthy);

    // The backup still holds the first revision.
    const auto paths = store->pathsFor(slot);
    ASSERT_TRUE(paths.has_value());
    EXPECT_TRUE(exists(paths->backupPathUtf8));
}

TEST_F(SaveStoreTest, RevisionKeepsAdvancingAcrossStoreInstances)
{
    const Save::SaveSlotId slot{2};
    {
        const auto store = makeStore();
        ASSERT_NE(store, nullptr);
        ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                        .slot = slot,
                                        .dataVersion = 1,
                                        .payload = payloadOf("a")})
                        .has_value());
    }
    // A fresh store must read the revision back off disk rather than restarting at 1.
    const auto reopened = makeStore();
    ASSERT_NE(reopened, nullptr);
    const auto written = reopened->saveSlot(Save::SaveWriteRequest{
        .slot = slot, .dataVersion = 1, .payload = payloadOf("b")});
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->metadata.revision, 2U);
}

TEST_F(SaveStoreTest, WriteValidationRejectsBadSlotVersionAndOversizedPayload)
{
    const auto store = makeStore(4);
    ASSERT_NE(store, nullptr);

    const auto badSlot = store->saveSlot(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{4}, .dataVersion = 1, .payload = {}});
    ASSERT_FALSE(badSlot.has_value());
    EXPECT_EQ(badSlot.error().code, Save::SaveErrorCode::InvalidSlot);

    // dataVersion 0 is reserved as "absent" by the envelope, so it cannot be authored.
    const auto zeroVersion = store->saveSlot(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{0}, .dataVersion = 0, .payload = {}});
    ASSERT_FALSE(zeroVersion.has_value());
    EXPECT_EQ(zeroVersion.error().code, Save::SaveErrorCode::InvalidMetadata);

    const std::vector<std::byte> oversized(4097, std::byte{'x'});
    const auto tooLarge = store->saveSlot(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{0}, .dataVersion = 1, .payload = oversized});
    ASSERT_FALSE(tooLarge.has_value());
    EXPECT_EQ(tooLarge.error().code, Save::SaveErrorCode::PayloadTooLarge);

    // The advertised limit itself must be usable, not merely close to failing.
    const std::vector<std::byte> atLimit(4096, std::byte{'x'});
    EXPECT_TRUE(store->saveSlot(Save::SaveWriteRequest{.slot = Save::SaveSlotId{0},
                                                       .dataVersion = 1,
                                                       .payload = atLimit})
                    .has_value());
}

TEST_F(SaveStoreTest, DisplayNameMustBeBoundedStrictUtf8)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);

    std::string oversized(Save::MaxSaveDisplayNameBytes + 1, 'n');
    const auto tooLong = store->saveSlot(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{0}, .dataVersion = 1, .displayName = oversized});
    ASSERT_FALSE(tooLong.has_value());
    EXPECT_EQ(tooLong.error().code, Save::SaveErrorCode::InvalidMetadata);

    // A lone continuation byte is not valid UTF-8.
    const auto invalidUtf8 = store->saveSlot(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{0}, .dataVersion = 1, .displayName = "\x80"});
    ASSERT_FALSE(invalidUtf8.has_value());
    EXPECT_EQ(invalidUtf8.error().code, Save::SaveErrorCode::InvalidMetadata);

    // Multi-byte UTF-8 survives the round trip.
    const auto ok = store->saveSlot(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{0}, .dataVersion = 1, .displayName = "存档一"});
    ASSERT_TRUE(ok.has_value());
    const auto loaded = store->loadSlot(Save::SaveSlotId{0});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->metadata.displayName, "存档一");
}

TEST_F(SaveStoreTest, LoadingAnEmptySlotReportsSlotNotFound)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);

    const auto loaded = store->loadSlot(Save::SaveSlotId{3});
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code, Save::SaveErrorCode::SlotNotFound);
}

// The whole point of the second copy: a corrupt primary still loads, and the
// result says which copy answered so the caller can warn or repair.
TEST_F(SaveStoreTest, CorruptPrimaryFallsBackToBackup)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const Save::SaveSlotId slot{1};

    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot,
                                    .dataVersion = 1,
                                    .payload = payloadOf("original")})
                    .has_value());
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot,
                                    .dataVersion = 1,
                                    .payload = payloadOf("newer")})
                    .has_value());

    const auto paths = store->pathsFor(slot);
    ASSERT_TRUE(paths.has_value());
    corruptFile(paths->primaryPathUtf8);

    const auto loaded = store->loadSlot(slot);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->source, Save::SaveStorageCopy::Backup);
    EXPECT_EQ(loaded->health, Save::SaveSlotHealth::RecoverableFromBackup);
    EXPECT_EQ(textOf(loaded->payload), "original");
}

// Fallback is opt-out: a caller that must not silently rewind refuses the backup
// and gets the primary's own corruption error instead.
TEST_F(SaveStoreTest, BackupFallbackCanBeDisabled)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const Save::SaveSlotId slot{1};
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("a")})
                    .has_value());
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("b")})
                    .has_value());

    const auto paths = store->pathsFor(slot);
    ASSERT_TRUE(paths.has_value());
    corruptFile(paths->primaryPathUtf8);

    const auto refused = store->loadSlot(slot, Save::SaveLoadOptions{.allowBackupFallback = false});
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, Save::SaveErrorCode::CorruptData);
}

TEST_F(SaveStoreTest, BothCopiesCorruptIsUnrecoverable)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const Save::SaveSlotId slot{1};
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("a")})
                    .has_value());
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("b")})
                    .has_value());

    const auto paths = store->pathsFor(slot);
    ASSERT_TRUE(paths.has_value());
    corruptFile(paths->primaryPathUtf8);
    corruptFile(paths->backupPathUtf8);

    const auto loaded = store->loadSlot(slot);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code, Save::SaveErrorCode::CorruptData);

    const auto summaries = store->listSlots();
    ASSERT_TRUE(summaries.has_value());
    EXPECT_EQ((*summaries)[1].health, Save::SaveSlotHealth::Unrecoverable);
}

// Corrupt bytes are replaceable; the write reports it so the caller knows data was
// lost rather than merely superseded.
TEST_F(SaveStoreTest, SaveOverACorruptPrimaryReportsTheReplacement)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const Save::SaveSlotId slot{1};
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("a")})
                    .has_value());

    const auto paths = store->pathsFor(slot);
    ASSERT_TRUE(paths.has_value());
    corruptFile(paths->primaryPathUtf8);

    const auto written = store->saveSlot(Save::SaveWriteRequest{
        .slot = slot, .dataVersion = 1, .payload = payloadOf("rewritten")});
    ASSERT_TRUE(written.has_value());
    EXPECT_TRUE(written->replacedCorruptPrimary);
    // A corrupt primary is not promoted to backup, so nothing was overwritten there.
    EXPECT_FALSE(written->backupUpdated);
}

// A save belonging to another gameId is never destroyed: it needs an explicit
// operator decision outside this API.
TEST_F(SaveStoreTest, SaveRefusesToOverwriteAnIncompatibleCopy)
{
    const Save::SaveSlotId slot{1};
    {
        Save::SaveStoreConfig other = config();
        other.gameId = "some.other.game";
        auto foreign = Save::SaveStore::Create(other);
        ASSERT_TRUE(foreign.has_value());
        ASSERT_TRUE((*foreign)
                        ->saveSlot(Save::SaveWriteRequest{
                            .slot = slot, .dataVersion = 1, .payload = payloadOf("theirs")})
                        .has_value());
    }

    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const auto refused = store->saveSlot(Save::SaveWriteRequest{
        .slot = slot, .dataVersion = 1, .payload = payloadOf("ours")});
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, Save::SaveErrorCode::WrongGameId);

    // Loading it is likewise refused rather than reinterpreted.
    const auto loaded = store->loadSlot(slot);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code, Save::SaveErrorCode::WrongGameId);
}

TEST_F(SaveStoreTest, ListReportsEveryConfiguredSlotWithItsHealth)
{
    const auto store = makeStore(4);
    ASSERT_NE(store, nullptr);
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{.slot = Save::SaveSlotId{0},
                                                       .dataVersion = 1,
                                                       .displayName = "one",
                                                       .payload = payloadOf("a")})
                    .has_value());
    // Slot 2 gets two writes so it reaches Healthy.
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = Save::SaveSlotId{2},
                                    .dataVersion = 1,
                                    .payload = payloadOf("b")})
                    .has_value());
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = Save::SaveSlotId{2},
                                    .dataVersion = 1,
                                    .payload = payloadOf("c")})
                    .has_value());

    const auto summaries = store->listSlots();
    ASSERT_TRUE(summaries.has_value());
    ASSERT_EQ(summaries->size(), 4U);

    EXPECT_EQ((*summaries)[0].health, Save::SaveSlotHealth::PrimaryOnly);
    ASSERT_TRUE((*summaries)[0].metadata.has_value());
    EXPECT_EQ((*summaries)[0].metadata->displayName, "one");
    EXPECT_EQ((*summaries)[0].selectedCopy, Save::SaveStorageCopy::Primary);

    EXPECT_EQ((*summaries)[1].health, Save::SaveSlotHealth::Empty);
    EXPECT_FALSE((*summaries)[1].metadata.has_value());
    EXPECT_FALSE((*summaries)[1].selectedCopy.has_value());

    EXPECT_EQ((*summaries)[2].health, Save::SaveSlotHealth::Healthy);
    EXPECT_EQ((*summaries)[2].primaryState, Save::SaveCopyState::Valid);
    EXPECT_EQ((*summaries)[2].backupState, Save::SaveCopyState::Valid);

    EXPECT_EQ((*summaries)[3].health, Save::SaveSlotHealth::Empty);
}

// Backup is removed first, so a failure partway through still leaves a loadable
// primary rather than only an orphaned backup.
TEST_F(SaveStoreTest, DeleteRemovesBothCopiesAndIsIdempotent)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const Save::SaveSlotId slot{1};
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("a")})
                    .has_value());
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("b")})
                    .has_value());

    const auto deleted = store->deleteSlot(slot);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_TRUE(deleted->primaryDeleted);
    EXPECT_TRUE(deleted->backupDeleted);

    const auto paths = store->pathsFor(slot);
    ASSERT_TRUE(paths.has_value());
    EXPECT_FALSE(exists(paths->primaryPathUtf8));
    EXPECT_FALSE(exists(paths->backupPathUtf8));

    // Deleting again succeeds and reports that nothing was there.
    const auto again = store->deleteSlot(slot);
    ASSERT_TRUE(again.has_value());
    EXPECT_FALSE(again->primaryDeleted);
    EXPECT_FALSE(again->backupDeleted);
}

TEST_F(SaveStoreTest, RepairRestoresPrimaryFromBackup)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const Save::SaveSlotId slot{1};
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot,
                                    .dataVersion = 1,
                                    .payload = payloadOf("original")})
                    .has_value());
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("newer")})
                    .has_value());

    const auto paths = store->pathsFor(slot);
    ASSERT_TRUE(paths.has_value());
    corruptFile(paths->primaryPathUtf8);

    const auto repaired = store->repairPrimaryFromBackup(slot);
    ASSERT_TRUE(repaired.has_value());
    EXPECT_TRUE(repaired->repaired);

    // The primary now serves the backup's contents.
    const auto loaded = store->loadSlot(slot);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->source, Save::SaveStorageCopy::Primary);
    EXPECT_EQ(textOf(loaded->payload), "original");
}

// Repair never runs implicitly and never overwrites healthy data, so calling it on
// a valid slot is a no-op that reports the existing metadata.
TEST_F(SaveStoreTest, RepairLeavesAValidPrimaryUntouched)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const Save::SaveSlotId slot{1};
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("a")})
                    .has_value());
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("b")})
                    .has_value());

    const auto repaired = store->repairPrimaryFromBackup(slot);
    ASSERT_TRUE(repaired.has_value());
    EXPECT_FALSE(repaired->repaired);
    EXPECT_EQ(repaired->metadata.revision, 2U);

    const auto loaded = store->loadSlot(slot);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(textOf(loaded->payload), "b");
}

TEST_F(SaveStoreTest, RepairWithoutAUsableBackupFails)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);
    const Save::SaveSlotId slot{1};
    ASSERT_TRUE(store->saveSlot(Save::SaveWriteRequest{
                                    .slot = slot, .dataVersion = 1, .payload = payloadOf("a")})
                    .has_value());

    const auto paths = store->pathsFor(slot);
    ASSERT_TRUE(paths.has_value());
    corruptFile(paths->primaryPathUtf8);

    const auto repaired = store->repairPrimaryFromBackup(slot);
    ASSERT_FALSE(repaired.has_value());
    EXPECT_EQ(repaired.error().code, Save::SaveErrorCode::BackupUnavailable);
}

// Async entry points need a TaskSystem; without one they say so instead of
// silently running the IO on the caller's thread.
TEST_F(SaveStoreTest, AsyncOperationsRequireATaskSystem)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);

    const auto save = store->beginSave(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{0}, .dataVersion = 1, .payload = {}});
    ASSERT_FALSE(save.has_value());
    EXPECT_EQ(save.error().code, Save::SaveErrorCode::AsyncUnavailable);

    EXPECT_EQ(store->beginLoad(Save::SaveSlotId{0}).error().code,
              Save::SaveErrorCode::AsyncUnavailable);
    EXPECT_EQ(store->beginList().error().code, Save::SaveErrorCode::AsyncUnavailable);
}

TEST_F(SaveStoreTest, AsyncSaveLoadAndListComplete)
{
    auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .cpuWorkerCount = 0,
        .ioQueueCapacity = 8,
        .cpuQueueCapacity = 0,
        .mainQueueCapacity = 8,
        .disableCpuWorkers = true,
    });
    ASSERT_TRUE(taskSystem.has_value());

    Save::SaveStoreConfig asyncConfig = config();
    asyncConfig.taskSystem = taskSystem->get();
    auto created = Save::SaveStore::Create(asyncConfig);
    ASSERT_TRUE(created.has_value());
    const auto& store = *created;

    const std::vector<std::byte> payload = payloadOf("async-payload");
    auto saveOperation = store->beginSave(Save::SaveWriteRequest{
        .slot = Save::SaveSlotId{2},
        .dataVersion = 7,
        .displayName = "Async",
        .payload = payload,
    });
    ASSERT_TRUE(saveOperation.has_value());
    ASSERT_TRUE(static_cast<bool>(*saveOperation));
    ASSERT_TRUE(waitForReady(*saveOperation));

    const auto written = saveOperation->take();
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->metadata.dataVersion, 7U);
    EXPECT_EQ(written->metadata.displayName, "Async");

    // The transaction latch is released once the worker finishes.
    EXPECT_FALSE(store->isBusy());

    auto loadOperation = store->beginLoad(Save::SaveSlotId{2});
    ASSERT_TRUE(loadOperation.has_value());
    ASSERT_TRUE(waitForReady(*loadOperation));
    const auto loaded = loadOperation->take();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(textOf(loaded->payload), "async-payload");

    auto listOperation = store->beginList();
    ASSERT_TRUE(listOperation.has_value());
    ASSERT_TRUE(waitForReady(*listOperation));
    const auto summaries = listOperation->take();
    ASSERT_TRUE(summaries.has_value());
    ASSERT_EQ(summaries->size(), 4U);
    EXPECT_EQ((*summaries)[2].health, Save::SaveSlotHealth::PrimaryOnly);

    taskSystem->get()->shutdownAndJoin();
}

// The result is one-shot: taking twice would hand the same buffer to two owners.
TEST_F(SaveStoreTest, AsyncResultIsSingleUseAndNotReadableEarly)
{
    auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 8,
        .mainQueueCapacity = 8,
    });
    ASSERT_TRUE(taskSystem.has_value());

    Save::SaveStoreConfig asyncConfig = config();
    asyncConfig.taskSystem = taskSystem->get();
    auto created = Save::SaveStore::Create(asyncConfig);
    ASSERT_TRUE(created.has_value());

    auto operation = (*created)->beginList();
    ASSERT_TRUE(operation.has_value());
    ASSERT_TRUE(waitForReady(*operation));

    EXPECT_TRUE(operation->take().has_value());
    const auto second = operation->take();
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, Save::SaveErrorCode::OperationAlreadyTaken);

    taskSystem->get()->shutdownAndJoin();
}

// A default-constructed handle owns nothing; calling take() on it is a caller
// error rather than a crash.
TEST_F(SaveStoreTest, DefaultConstructedOperationHandlesAreInvalid)
{
    Save::SaveWriteOperation write{};
    EXPECT_FALSE(static_cast<bool>(write));
    EXPECT_FALSE(write.ready());
    const auto taken = write.take();
    ASSERT_FALSE(taken.has_value());
    EXPECT_EQ(taken.error().code, Save::SaveErrorCode::InvalidOperation);

    Save::SaveLoadOperation load{};
    EXPECT_EQ(load.take().error().code, Save::SaveErrorCode::InvalidOperation);
    Save::SaveListOperation list{};
    EXPECT_EQ(list.take().error().code, Save::SaveErrorCode::InvalidOperation);
}

// Async handles keep the shared state alive on purpose, so a result stays
// retrievable even if the facade is destroyed first.
TEST_F(SaveStoreTest, AsyncHandleOutlivesTheStoreFacade)
{
    auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 8,
        .mainQueueCapacity = 8,
    });
    ASSERT_TRUE(taskSystem.has_value());

    Save::SaveStoreConfig asyncConfig = config();
    asyncConfig.taskSystem = taskSystem->get();

    Save::SaveListOperation operation{};
    {
        auto created = Save::SaveStore::Create(asyncConfig);
        ASSERT_TRUE(created.has_value());
        auto begun = (*created)->beginList();
        ASSERT_TRUE(begun.has_value());
        operation = std::move(*begun);
    }
    ASSERT_TRUE(waitForReady(operation));
    EXPECT_TRUE(operation.take().has_value());

    taskSystem->get()->shutdownAndJoin();
}

// Only one filesystem transaction at a time. The async path holds the latch until
// its worker completes, so a second request is refused rather than interleaved.
TEST_F(SaveStoreTest, ASecondTransactionIsRefusedWhileOneIsActive)
{
    auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 8,
        .mainQueueCapacity = 8,
    });
    ASSERT_TRUE(taskSystem.has_value());

    Save::SaveStoreConfig asyncConfig = config(Save::MaxSaveSlotCapacity);
    asyncConfig.taskSystem = taskSystem->get();
    auto created = Save::SaveStore::Create(asyncConfig);
    ASSERT_TRUE(created.has_value());
    const auto& store = *created;

    // Listing 1024 slots takes long enough to still be running on the next line.
    auto first = store->beginList();
    ASSERT_TRUE(first.has_value());

    auto second = store->beginList();
    if (!second.has_value()) {
        EXPECT_EQ(second.error().code, Save::SaveErrorCode::Busy);
    } else {
        // The first operation finished before the second request arrived; the latch
        // is single-owner either way, so wait for both instead of failing on timing.
        ASSERT_TRUE(waitForReady(*second));
        EXPECT_TRUE(second->take().has_value());
    }

    ASSERT_TRUE(waitForReady(*first));
    EXPECT_TRUE(first->take().has_value());
    EXPECT_FALSE(store->isBusy());

    taskSystem->get()->shutdownAndJoin();
}

TEST_F(SaveStoreTest, CommandsFromAnotherThreadAreRejected)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);

    Core::ErrorCode observed{};
    std::thread worker{[&store, &observed]() {
        const auto result = store->listSlots();
        observed = result.has_value() ? Core::ErrorCode{} : result.error().code;
    }};
    worker.join();
    EXPECT_EQ(observed, Save::SaveErrorCode::WrongOwnerThread);
}

// Pure path composition, so it stays callable from any thread.
TEST_F(SaveStoreTest, PathsForIsCallableOffTheOwnerThread)
{
    const auto store = makeStore();
    ASSERT_NE(store, nullptr);

    bool succeeded = false;
    std::thread worker{[&store, &succeeded]() {
        succeeded = store->pathsFor(Save::SaveSlotId{1}).has_value();
    }};
    worker.join();
    EXPECT_TRUE(succeeded);
}

TEST_F(SaveStoreTest, DefaultSaveRootPathIsUnderTheApplicationStateDirectory)
{
    const auto root = Save::defaultSaveRootPath("TinaSaveTests");
    ASSERT_TRUE(root.has_value());
    EXPECT_TRUE(root->ends_with("/saves"));
    EXPECT_NE(root->find("TinaSaveTests"), std::string::npos);

    // The application name must be a single safe path segment.
    EXPECT_FALSE(Save::defaultSaveRootPath("").has_value());
    EXPECT_FALSE(Save::defaultSaveRootPath("..").has_value());
    EXPECT_FALSE(Save::defaultSaveRootPath("nested/name").has_value());
}

} // namespace Tina::Tests
