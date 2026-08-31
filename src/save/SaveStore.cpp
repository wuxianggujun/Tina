#include <tina/save/SaveStore.hpp>

#include "SaveFormat.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/UserPaths.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/save/SaveErrors.hpp>
#include <tina/task/TaskSystem.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::Save::Detail {

template <typename Value>
class SaveAsyncState final {
  public:
    static_assert(std::is_nothrow_move_constructible_v<Core::Result<Value>>);

    [[nodiscard]] bool ready() const noexcept
    {
        return ready_.load(std::memory_order_acquire);
    }

    void publish(Core::Result<Value> result) noexcept
    {
        result_.emplace(std::move(result));
        ready_.store(true, std::memory_order_release);
    }

    [[nodiscard]] Core::Result<Value> take()
    {
        if (!ready())
        {
            return Core::failure(SaveErrorCode::OperationNotReady,
                                 "save operation has not completed");
        }

        bool expected = false;
        if (!taken_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return Core::failure(SaveErrorCode::OperationAlreadyTaken,
                                 "save operation result was already taken");
        }
        return std::move(*result_);
    }

  private:
    std::optional<Core::Result<Value>> result_{};
    std::atomic<bool> ready_{false};
    std::atomic<bool> taken_{false};
};

class SaveStoreState final {
  public:
    explicit SaveStoreState(SaveStoreConfig value) noexcept
        : config(std::move(value)), ownerThread(std::this_thread::get_id())
    {
    }

    [[nodiscard]] Core::Status requireOwnerThread() const
    {
        if (std::this_thread::get_id() != ownerThread)
        {
            return Core::failure(SaveErrorCode::WrongOwnerThread,
                                 "SaveStore command must run on its owner thread");
        }
        return Core::success();
    }

    [[nodiscard]] bool tryBeginTransaction() noexcept
    {
        bool expected = false;
        return transactionActive.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel);
    }

    void finishTransaction() noexcept
    {
        transactionActive.store(false, std::memory_order_release);
    }

    SaveStoreConfig config{};
    std::thread::id ownerThread{};
    std::atomic<bool> transactionActive{false};
};

} // namespace Tina::Save::Detail

namespace Tina::Save {
namespace {

struct CopyInspection final {
    SaveCopyState state = SaveCopyState::Missing;
    std::optional<Detail::ParsedSaveFile> parsed{};
    std::pmr::vector<std::byte> bytes{std::pmr::get_default_resource()};
    std::optional<Core::Error> error{};
};

struct OwnedSaveWriteRequest final {
    SaveSlotId slot{};
    Core::u32 dataVersion = 0;
    std::string displayName{};
    std::vector<std::byte> payload{};
    Core::u64 playTimeMilliseconds = 0;

    [[nodiscard]] SaveWriteRequest view() const noexcept
    {
        return SaveWriteRequest{
            .slot = slot,
            .dataVersion = dataVersion,
            .displayName = displayName,
            .payload = payload,
            .playTimeMilliseconds = playTimeMilliseconds,
        };
    }
};

[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view pathUtf8)
{
    return std::filesystem::u8path(pathUtf8.begin(), pathUtf8.end());
}

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t value : encoded)
    {
        result.push_back(static_cast<char>(value));
    }
    return result;
}

[[nodiscard]] std::string joinPath(std::string_view root, std::string_view leaf)
{
    std::string result{root};
    if (!result.empty() && result.back() != '/' && result.back() != '\\')
    {
        result.push_back('/');
    }
    result.append(leaf);
    return result;
}

[[nodiscard]] Core::Error filesystemError(
    std::string_view message,
    std::string_view path,
    const std::error_code& errorCode)
{
    const Core::ErrorCode code = errorCode == std::errc::permission_denied
                                     ? Core::CoreErrorCode::PermissionDenied
                                     : Core::CoreErrorCode::Io;
    Core::Error error{code, message};
    error.addContext("path", path);
    if (errorCode)
    {
        error.setNativeCode(static_cast<Core::i64>(errorCode.value()));
        error.addContext("native", errorCode.message());
    }
    return error;
}

template <typename Value, typename Work>
[[nodiscard]] Core::Result<Value> executeGuarded(
    Work&& work,
    std::string_view failureMessage) noexcept
{
    try
    {
        return std::invoke(std::forward<Work>(work));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "save operation allocation failed");
    }
    catch (const std::filesystem::filesystem_error& error)
    {
        Core::Error failure{Core::CoreErrorCode::Io, failureMessage};
        failure.setNativeCode(static_cast<Core::i64>(error.code().value()));
        failure.addContext("native", error.code().message());
        return Core::failure(std::move(failure));
    }
    catch (const std::exception&)
    {
        return Core::failure(Core::CoreErrorCode::Internal, failureMessage);
    }
    catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal, failureMessage);
    }
}

[[nodiscard]] Core::Status validateConfig(const SaveStoreConfig& config)
{
    if (config.rootDirectoryUtf8.empty() ||
        !Core::isStrictUtf8WithoutNul(config.rootDirectoryUtf8))
    {
        return Core::failure(SaveErrorCode::InvalidConfiguration,
                             "save root must be non-empty strict UTF-8 without NUL");
    }
    if (!pathFromUtf8(config.rootDirectoryUtf8).is_absolute())
    {
        return Core::failure(SaveErrorCode::InvalidConfiguration,
                             "save root must be an absolute path");
    }
    if (config.gameId.empty() || config.gameId.size() > MaxSaveGameIdBytes ||
        !Core::isStrictUtf8WithoutNul(config.gameId))
    {
        return Core::failure(SaveErrorCode::InvalidConfiguration,
                             "save gameId must be bounded strict UTF-8 without NUL");
    }
    if (config.slotCapacity == 0 || config.slotCapacity > MaxSaveSlotCapacity ||
        config.maxPayloadBytes == 0 || config.maxPayloadBytes > MaxSavePayloadBytes)
    {
        return Core::failure(SaveErrorCode::InvalidConfiguration,
                             "save slot or payload limits are zero or exceed hard limits");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateSlot(
    const Detail::SaveStoreState& state,
    SaveSlotId slot)
{
    if (slot.value >= state.config.slotCapacity)
    {
        return Core::failure(SaveErrorCode::InvalidSlot,
                             "save slot is outside the configured fixed slot table");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateWriteRequest(
    const Detail::SaveStoreState& state,
    const SaveWriteRequest& request)
{
    if (const auto status = validateSlot(state, request.slot); !status)
    {
        return status;
    }
    if (request.dataVersion == 0)
    {
        return Core::failure(SaveErrorCode::InvalidMetadata,
                             "save dataVersion must be non-zero");
    }
    if (request.displayName.size() > MaxSaveDisplayNameBytes ||
        !Core::isStrictUtf8WithoutNul(request.displayName))
    {
        return Core::failure(SaveErrorCode::InvalidMetadata,
                             "save displayName must be bounded strict UTF-8 without NUL");
    }
    if (static_cast<Core::u64>(request.payload.size()) > state.config.maxPayloadBytes)
    {
        return Core::failure(SaveErrorCode::PayloadTooLarge,
                             "save payload exceeds the configured limit");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<SaveStorePaths> pathsForState(
    const Detail::SaveStoreState& state,
    SaveSlotId slot)
{
    if (const auto status = validateSlot(state, slot); !status)
    {
        return Core::failure(status.error());
    }

    std::string filename{"slot-"};
    const std::string digits = std::to_string(slot.value);
    filename.append(4U - digits.size(), '0');
    filename.append(digits);
    filename.append(".tsave");

    std::string primary = joinPath(state.config.rootDirectoryUtf8, filename);
    std::string backup = primary;
    backup.append(".bak");
    return SaveStorePaths{
        .primaryPathUtf8 = std::move(primary),
        .backupPathUtf8 = std::move(backup),
    };
}

[[nodiscard]] Core::u64 maximumEnvelopeBytes(const Detail::SaveStoreState& state) noexcept
{
    return state.config.maxPayloadBytes + Detail::SaveWireHeaderBytes +
           Detail::SaveWireDigestBytes + MaxSaveGameIdBytes + MaxSaveDisplayNameBytes;
}

[[nodiscard]] Core::Result<CopyInspection> inspectCopy(
    const Detail::SaveStoreState& state,
    SaveSlotId slot,
    std::string_view path)
{
    auto bytes = Core::readFile(path, Core::ReadFileConfig{
                                          .maxBytes = maximumEnvelopeBytes(state),
                                          .memoryResource = std::pmr::get_default_resource(),
                                      });
    if (!bytes)
    {
        if (bytes.error().code == Core::CoreErrorCode::NotFound)
        {
            return CopyInspection{};
        }
        if (bytes.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            CopyInspection inspection{};
            inspection.state = SaveCopyState::Incompatible;
            inspection.error.emplace(SaveErrorCode::PayloadTooLarge,
                                     "save file exceeds the configured envelope limit");
            inspection.error->addContext("path", path);
            return inspection;
        }
        return Core::failure(std::move(bytes.error()).withContext("SaveStore::inspectCopy", path));
    }

    auto parsed = Detail::parseSaveFile(*bytes, slot, state.config.gameId,
                                        state.config.maxPayloadBytes);
    if (!parsed)
    {
        const Core::ErrorCode code = parsed.error().code;
        if (code != SaveErrorCode::CorruptData &&
            code != SaveErrorCode::UnsupportedSchema &&
            code != SaveErrorCode::WrongGameId &&
            code != SaveErrorCode::PayloadTooLarge)
        {
            return Core::failure(std::move(parsed.error()).withContext(
                "SaveStore::inspectCopy", path));
        }

        CopyInspection inspection{};
        inspection.state = code == SaveErrorCode::CorruptData
                               ? SaveCopyState::Corrupt
                               : SaveCopyState::Incompatible;
        inspection.error.emplace(std::move(parsed.error()));
        inspection.error->addContext("path", path);
        return inspection;
    }

    CopyInspection inspection{};
    inspection.state = SaveCopyState::Valid;
    inspection.parsed.emplace(std::move(*parsed));
    inspection.bytes = std::move(*bytes);
    return inspection;
}

[[nodiscard]] SaveSlotHealth healthFor(
    SaveCopyState primary,
    SaveCopyState backup) noexcept
{
    const bool primaryValid = primary == SaveCopyState::Valid;
    const bool backupValid = backup == SaveCopyState::Valid;
    if (primaryValid && backupValid)
    {
        return SaveSlotHealth::Healthy;
    }
    if (primaryValid)
    {
        return SaveSlotHealth::PrimaryOnly;
    }
    if (backupValid)
    {
        return SaveSlotHealth::RecoverableFromBackup;
    }
    if (primary == SaveCopyState::Missing && backup == SaveCopyState::Missing)
    {
        return SaveSlotHealth::Empty;
    }
    return SaveSlotHealth::Unrecoverable;
}

[[nodiscard]] SaveSlotSummary summarize(
    SaveSlotId slot,
    const CopyInspection& primary,
    const CopyInspection& backup)
{
    SaveSlotSummary summary{
        .slot = slot,
        .primaryState = primary.state,
        .backupState = backup.state,
        .health = healthFor(primary.state, backup.state),
    };
    if (primary.state == SaveCopyState::Valid)
    {
        summary.selectedCopy = SaveStorageCopy::Primary;
        summary.metadata = primary.parsed->metadata;
    }
    else if (backup.state == SaveCopyState::Valid)
    {
        summary.selectedCopy = SaveStorageCopy::Backup;
        summary.metadata = backup.parsed->metadata;
    }
    return summary;
}

[[nodiscard]] Core::Result<SaveLoadResult> loadFromCopy(
    const CopyInspection& inspection,
    SaveStorageCopy source,
    SaveSlotHealth health)
{
    const auto& parsed = *inspection.parsed;
    const Core::usize payloadBytes = static_cast<Core::usize>(parsed.metadata.payloadBytes);
    std::vector<std::byte> payload;
    payload.assign(inspection.bytes.begin() + parsed.payloadOffset,
                   inspection.bytes.begin() + parsed.payloadOffset + payloadBytes);
    return SaveLoadResult{
        .metadata = parsed.metadata,
        .payload = std::move(payload),
        .source = source,
        .health = health,
    };
}

[[nodiscard]] Core::Result<SaveLoadResult> unavailableLoadFailure(
    const CopyInspection& primary,
    const CopyInspection& backup,
    bool allowBackupFallback)
{
    if (primary.state != SaveCopyState::Missing && primary.error)
    {
        return Core::failure(*primary.error);
    }
    if (allowBackupFallback && backup.state != SaveCopyState::Missing && backup.error)
    {
        return Core::failure(*backup.error);
    }
    return Core::failure(SaveErrorCode::SlotNotFound,
                         allowBackupFallback
                             ? "save slot has no loadable primary or backup"
                             : "save slot has no loadable primary");
}

[[nodiscard]] Core::Result<Core::u64> unixMillisecondsNow()
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    if (elapsed.count() < 0)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "system clock precedes the Unix epoch");
    }
    return static_cast<Core::u64>(elapsed.count());
}

[[nodiscard]] Core::Result<SaveWriteResult> performSave(
    Detail::SaveStoreState& state,
    const SaveWriteRequest& request)
{
    if (const auto status = validateWriteRequest(state, request); !status)
    {
        return Core::failure(status.error());
    }
    auto paths = pathsForState(state, request.slot);
    if (!paths)
    {
        return Core::failure(std::move(paths.error()));
    }

    auto primary = inspectCopy(state, request.slot, paths->primaryPathUtf8);
    if (!primary)
    {
        return Core::failure(std::move(primary.error()));
    }
    auto backup = inspectCopy(state, request.slot, paths->backupPathUtf8);
    if (!backup)
    {
        return Core::failure(std::move(backup.error()));
    }

    // Never destroy a save written by another game or a newer envelope reader.
    // Corrupt bytes may be replaced, but incompatible bytes require an explicit
    // operator decision outside this API.
    if (primary->state == SaveCopyState::Incompatible)
    {
        return primary->error
                   ? Core::failure(*primary->error)
                   : Core::failure(SaveErrorCode::UnsupportedSchema,
                                   "primary save copy is incompatible");
    }
    if (backup->state == SaveCopyState::Incompatible)
    {
        return backup->error
                   ? Core::failure(*backup->error)
                   : Core::failure(SaveErrorCode::UnsupportedSchema,
                                   "backup save copy is incompatible");
    }

    Core::u64 latestRevision = 0;
    if (primary->state == SaveCopyState::Valid)
    {
        latestRevision = primary->parsed->metadata.revision;
    }
    if (backup->state == SaveCopyState::Valid)
    {
        latestRevision = (std::max)(latestRevision, backup->parsed->metadata.revision);
    }
    if (latestRevision == (std::numeric_limits<Core::u64>::max)())
    {
        return Core::failure(SaveErrorCode::RevisionOverflow,
                             "save revision cannot advance beyond uint64 max");
    }

    auto savedAt = unixMillisecondsNow();
    if (!savedAt)
    {
        return Core::failure(std::move(savedAt.error()));
    }
    SaveSlotMetadata metadata{
        .slot = request.slot,
        .dataVersion = request.dataVersion,
        .revision = latestRevision + 1U,
        .savedAtUnixMilliseconds = *savedAt,
        .playTimeMilliseconds = request.playTimeMilliseconds,
        .gameId = state.config.gameId,
        .displayName = request.displayName,
        .payloadBytes = static_cast<Core::u64>(request.payload.size()),
    };
    auto encoded = Detail::encodeSaveFile(metadata, request.payload);
    if (!encoded)
    {
        return Core::failure(std::move(encoded.error()));
    }

    bool backupUpdated = false;
    if (primary->state == SaveCopyState::Valid &&
        (backup->state != SaveCopyState::Valid ||
         backup->parsed->metadata.revision <= primary->parsed->metadata.revision))
    {
        if (auto status = Core::writeFile(paths->backupPathUtf8, primary->bytes); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "SaveStore::saveSlot", "update backup"));
        }
        backupUpdated = true;
    }

    if (auto status = Core::writeFile(paths->primaryPathUtf8, *encoded); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "SaveStore::saveSlot", "replace primary"));
    }
    return SaveWriteResult{
        .metadata = std::move(metadata),
        .backupUpdated = backupUpdated,
        .replacedCorruptPrimary = primary->state == SaveCopyState::Corrupt,
    };
}

[[nodiscard]] Core::Result<SaveLoadResult> performLoad(
    Detail::SaveStoreState& state,
    SaveSlotId slot,
    SaveLoadOptions options)
{
    auto paths = pathsForState(state, slot);
    if (!paths)
    {
        return Core::failure(std::move(paths.error()));
    }
    auto primary = inspectCopy(state, slot, paths->primaryPathUtf8);
    if (!primary)
    {
        return Core::failure(std::move(primary.error()));
    }
    auto backup = inspectCopy(state, slot, paths->backupPathUtf8);
    if (!backup)
    {
        return Core::failure(std::move(backup.error()));
    }

    const SaveSlotHealth health = healthFor(primary->state, backup->state);
    if (primary->state == SaveCopyState::Valid)
    {
        return loadFromCopy(*primary, SaveStorageCopy::Primary, health);
    }
    if (options.allowBackupFallback && backup->state == SaveCopyState::Valid)
    {
        return loadFromCopy(*backup, SaveStorageCopy::Backup, health);
    }
    return unavailableLoadFailure(*primary, *backup, options.allowBackupFallback);
}

[[nodiscard]] Core::Result<std::vector<SaveSlotSummary>> performList(
    Detail::SaveStoreState& state)
{
    std::vector<SaveSlotSummary> summaries;
    summaries.reserve(state.config.slotCapacity);
    for (Core::u32 index = 0; index < state.config.slotCapacity; ++index)
    {
        const SaveSlotId slot{index};
        auto paths = pathsForState(state, slot);
        if (!paths)
        {
            return Core::failure(std::move(paths.error()));
        }
        auto primary = inspectCopy(state, slot, paths->primaryPathUtf8);
        if (!primary)
        {
            return Core::failure(std::move(primary.error()));
        }
        auto backup = inspectCopy(state, slot, paths->backupPathUtf8);
        if (!backup)
        {
            return Core::failure(std::move(backup.error()));
        }
        summaries.push_back(summarize(slot, *primary, *backup));
    }
    return summaries;
}

[[nodiscard]] Core::Result<bool> removeFile(std::string_view path)
{
    std::error_code errorCode;
    const bool removed = std::filesystem::remove(pathFromUtf8(path), errorCode);
    if (errorCode)
    {
        return Core::failure(filesystemError("failed to remove save file", path, errorCode));
    }
    return removed;
}

[[nodiscard]] Core::Result<SaveDeleteResult> performDelete(
    Detail::SaveStoreState& state,
    SaveSlotId slot)
{
    auto paths = pathsForState(state, slot);
    if (!paths)
    {
        return Core::failure(std::move(paths.error()));
    }

    auto backupDeleted = removeFile(paths->backupPathUtf8);
    if (!backupDeleted)
    {
        return Core::failure(std::move(backupDeleted.error()));
    }
    auto primaryDeleted = removeFile(paths->primaryPathUtf8);
    if (!primaryDeleted)
    {
        return Core::failure(std::move(primaryDeleted.error()));
    }
    return SaveDeleteResult{
        .slot = slot,
        .primaryDeleted = *primaryDeleted,
        .backupDeleted = *backupDeleted,
    };
}

[[nodiscard]] Core::Result<SaveRepairResult> performRepair(
    Detail::SaveStoreState& state,
    SaveSlotId slot)
{
    auto paths = pathsForState(state, slot);
    if (!paths)
    {
        return Core::failure(std::move(paths.error()));
    }
    auto primary = inspectCopy(state, slot, paths->primaryPathUtf8);
    if (!primary)
    {
        return Core::failure(std::move(primary.error()));
    }
    if (primary->state == SaveCopyState::Valid)
    {
        return SaveRepairResult{
            .metadata = primary->parsed->metadata,
            .repaired = false,
        };
    }
    if (primary->state == SaveCopyState::Incompatible)
    {
        return primary->error
                   ? Core::failure(*primary->error)
                   : Core::failure(SaveErrorCode::UnsupportedSchema,
                                   "primary save copy is incompatible");
    }

    auto backup = inspectCopy(state, slot, paths->backupPathUtf8);
    if (!backup)
    {
        return Core::failure(std::move(backup.error()));
    }
    if (backup->state != SaveCopyState::Valid)
    {
        Core::Error error{SaveErrorCode::BackupUnavailable,
                          "save slot has no valid backup to repair from"};
        if (backup->error)
        {
            error.addContext("backup", backup->error->message);
        }
        return Core::failure(std::move(error));
    }

    if (auto status = Core::writeFile(paths->primaryPathUtf8, backup->bytes); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "SaveStore::repairPrimaryFromBackup", "replace primary"));
    }
    return SaveRepairResult{
        .metadata = backup->parsed->metadata,
        .repaired = true,
    };
}

template <typename Value, typename Work>
[[nodiscard]] Core::Status scheduleOperation(
    const std::shared_ptr<Detail::SaveStoreState>& store,
    const std::shared_ptr<Detail::SaveAsyncState<Value>>& completion,
    Work&& work)
{
    try
    {
        Task::TaskCallable task{
            [store, completion, work = std::forward<Work>(work)]() mutable noexcept {
                auto result = executeGuarded<Value>(
                    [&]() { return std::invoke(work, *store); },
                    "asynchronous save operation failed");
                store->finishTransaction();
                completion->publish(std::move(result));
            }};
        return store->config.taskSystem->scheduleIo(std::move(task));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "save task scheduling allocation failed");
    }
    catch (const std::exception&)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "task system threw while scheduling save IO");
    }
    catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "task system failed while scheduling save IO");
    }
}

[[nodiscard]] Core::Status requireAsyncStore(const Detail::SaveStoreState& state)
{
    if (const auto status = state.requireOwnerThread(); !status)
    {
        return status;
    }
    if (state.config.taskSystem == nullptr)
    {
        return Core::failure(SaveErrorCode::AsyncUnavailable,
                             "SaveStore has no TaskSystem for asynchronous IO");
    }
    return Core::success();
}

template <typename Value>
[[nodiscard]] Core::Result<Value> invalidOperationHandle()
{
    return Core::failure(SaveErrorCode::InvalidOperation,
                         "save operation handle is empty");
}

} // namespace

Core::Result<std::string> defaultSaveRootPath(std::string_view applicationName)
try
{
    auto applicationRoot = Core::userApplicationDirectory(
        applicationName, Core::UserDirectoryKind::State);
    if (!applicationRoot)
    {
        return Core::failure(std::move(applicationRoot.error()));
    }
    return joinPath(*applicationRoot, "saves");
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "default save root allocation failed");
}

SaveWriteOperation::SaveWriteOperation(
    std::shared_ptr<Detail::SaveAsyncState<SaveWriteResult>> state) noexcept
    : state_(std::move(state))
{
}

SaveWriteOperation::operator bool() const noexcept
{
    return state_ != nullptr;
}

bool SaveWriteOperation::ready() const noexcept
{
    return state_ != nullptr && state_->ready();
}

Core::Result<SaveWriteResult> SaveWriteOperation::take()
{
    return state_ ? state_->take() : invalidOperationHandle<SaveWriteResult>();
}

SaveLoadOperation::SaveLoadOperation(
    std::shared_ptr<Detail::SaveAsyncState<SaveLoadResult>> state) noexcept
    : state_(std::move(state))
{
}

SaveLoadOperation::operator bool() const noexcept
{
    return state_ != nullptr;
}

bool SaveLoadOperation::ready() const noexcept
{
    return state_ != nullptr && state_->ready();
}

Core::Result<SaveLoadResult> SaveLoadOperation::take()
{
    return state_ ? state_->take() : invalidOperationHandle<SaveLoadResult>();
}

SaveListOperation::SaveListOperation(
    std::shared_ptr<Detail::SaveAsyncState<std::vector<SaveSlotSummary>>> state) noexcept
    : state_(std::move(state))
{
}

SaveListOperation::operator bool() const noexcept
{
    return state_ != nullptr;
}

bool SaveListOperation::ready() const noexcept
{
    return state_ != nullptr && state_->ready();
}

Core::Result<std::vector<SaveSlotSummary>> SaveListOperation::take()
{
    return state_ ? state_->take()
                  : invalidOperationHandle<std::vector<SaveSlotSummary>>();
}

SaveStore::SaveStore(std::shared_ptr<Detail::SaveStoreState> state) noexcept
    : state_(std::move(state))
{
}

SaveStore::~SaveStore() noexcept = default;

Core::Result<std::unique_ptr<SaveStore>> SaveStore::Create(SaveStoreConfig config)
try
{
    if (const auto status = validateConfig(config); !status)
    {
        return Core::failure(status.error());
    }
    config.rootDirectoryUtf8 = pathToUtf8(
        pathFromUtf8(config.rootDirectoryUtf8).lexically_normal());
    auto state = std::make_shared<Detail::SaveStoreState>(std::move(config));
    return std::unique_ptr<SaveStore>{new SaveStore{std::move(state)}};
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "SaveStore allocation failed");
}
catch (const std::filesystem::filesystem_error& error)
{
    Core::Error failure{SaveErrorCode::InvalidConfiguration,
                        "save root path could not be normalized"};
    failure.setNativeCode(static_cast<Core::i64>(error.code().value()));
    return Core::failure(std::move(failure));
}
catch (const std::exception&)
{
    return Core::failure(SaveErrorCode::InvalidConfiguration,
                         "save root path could not be normalized");
}

Core::Result<SaveStorePaths> SaveStore::pathsFor(SaveSlotId slot) const
{
    return executeGuarded<SaveStorePaths>(
        [&]() { return pathsForState(*state_, slot); },
        "save path composition failed");
}

bool SaveStore::isBusy() const noexcept
{
    return state_->transactionActive.load(std::memory_order_acquire);
}

Core::Result<SaveWriteResult> SaveStore::saveSlot(const SaveWriteRequest& request)
{
    if (const auto status = state_->requireOwnerThread(); !status)
    {
        return Core::failure(status.error());
    }
    if (!state_->tryBeginTransaction())
    {
        return Core::failure(SaveErrorCode::Busy,
                             "SaveStore already has an active transaction");
    }
    auto finish = Core::makeScopeExit([state = state_]() noexcept {
        state->finishTransaction();
    });
    return executeGuarded<SaveWriteResult>(
        [&]() { return performSave(*state_, request); },
        "save slot write failed");
}

Core::Result<SaveLoadResult> SaveStore::loadSlot(
    SaveSlotId slot,
    SaveLoadOptions options)
{
    if (const auto status = state_->requireOwnerThread(); !status)
    {
        return Core::failure(status.error());
    }
    if (!state_->tryBeginTransaction())
    {
        return Core::failure(SaveErrorCode::Busy,
                             "SaveStore already has an active transaction");
    }
    auto finish = Core::makeScopeExit([state = state_]() noexcept {
        state->finishTransaction();
    });
    return executeGuarded<SaveLoadResult>(
        [&]() { return performLoad(*state_, slot, options); },
        "save slot load failed");
}

Core::Result<std::vector<SaveSlotSummary>> SaveStore::listSlots()
{
    if (const auto status = state_->requireOwnerThread(); !status)
    {
        return Core::failure(status.error());
    }
    if (!state_->tryBeginTransaction())
    {
        return Core::failure(SaveErrorCode::Busy,
                             "SaveStore already has an active transaction");
    }
    auto finish = Core::makeScopeExit([state = state_]() noexcept {
        state->finishTransaction();
    });
    return executeGuarded<std::vector<SaveSlotSummary>>(
        [&]() { return performList(*state_); },
        "save slot listing failed");
}

Core::Result<SaveDeleteResult> SaveStore::deleteSlot(SaveSlotId slot)
{
    if (const auto status = state_->requireOwnerThread(); !status)
    {
        return Core::failure(status.error());
    }
    if (!state_->tryBeginTransaction())
    {
        return Core::failure(SaveErrorCode::Busy,
                             "SaveStore already has an active transaction");
    }
    auto finish = Core::makeScopeExit([state = state_]() noexcept {
        state->finishTransaction();
    });
    return executeGuarded<SaveDeleteResult>(
        [&]() { return performDelete(*state_, slot); },
        "save slot deletion failed");
}

Core::Result<SaveRepairResult> SaveStore::repairPrimaryFromBackup(SaveSlotId slot)
{
    if (const auto status = state_->requireOwnerThread(); !status)
    {
        return Core::failure(status.error());
    }
    if (!state_->tryBeginTransaction())
    {
        return Core::failure(SaveErrorCode::Busy,
                             "SaveStore already has an active transaction");
    }
    auto finish = Core::makeScopeExit([state = state_]() noexcept {
        state->finishTransaction();
    });
    return executeGuarded<SaveRepairResult>(
        [&]() { return performRepair(*state_, slot); },
        "save slot repair failed");
}

Core::Result<SaveWriteOperation> SaveStore::beginSave(const SaveWriteRequest& request)
{
    if (const auto status = requireAsyncStore(*state_); !status)
    {
        return Core::failure(status.error());
    }
    if (const auto status = validateWriteRequest(*state_, request); !status)
    {
        return Core::failure(status.error());
    }
    if (!state_->tryBeginTransaction())
    {
        return Core::failure(SaveErrorCode::Busy,
                             "SaveStore already has an active transaction");
    }
    auto finishOnFailure = Core::makeScopeExit([state = state_]() noexcept {
        state->finishTransaction();
    });

    try
    {
        auto owned = std::make_shared<OwnedSaveWriteRequest>();
        owned->slot = request.slot;
        owned->dataVersion = request.dataVersion;
        owned->displayName = request.displayName;
        owned->payload.assign(request.payload.begin(), request.payload.end());
        owned->playTimeMilliseconds = request.playTimeMilliseconds;
        auto completion =
            std::make_shared<Detail::SaveAsyncState<SaveWriteResult>>();
        auto scheduled = scheduleOperation<SaveWriteResult>(
            state_, completion,
            [owned](Detail::SaveStoreState& state) {
                const SaveWriteRequest view = owned->view();
                return performSave(state, view);
            });
        if (!scheduled)
        {
            return Core::failure(std::move(scheduled.error()).withContext(
                "SaveStore::beginSave", "schedule IO"));
        }
        finishOnFailure.release();
        return SaveWriteOperation{std::move(completion)};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "asynchronous save request allocation failed");
    }
}

Core::Result<SaveLoadOperation> SaveStore::beginLoad(
    SaveSlotId slot,
    SaveLoadOptions options)
{
    if (const auto status = requireAsyncStore(*state_); !status)
    {
        return Core::failure(status.error());
    }
    if (const auto status = validateSlot(*state_, slot); !status)
    {
        return Core::failure(status.error());
    }
    if (!state_->tryBeginTransaction())
    {
        return Core::failure(SaveErrorCode::Busy,
                             "SaveStore already has an active transaction");
    }
    auto finishOnFailure = Core::makeScopeExit([state = state_]() noexcept {
        state->finishTransaction();
    });

    try
    {
        auto completion =
            std::make_shared<Detail::SaveAsyncState<SaveLoadResult>>();
        auto scheduled = scheduleOperation<SaveLoadResult>(
            state_, completion,
            [slot, options](Detail::SaveStoreState& state) {
                return performLoad(state, slot, options);
            });
        if (!scheduled)
        {
            return Core::failure(std::move(scheduled.error()).withContext(
                "SaveStore::beginLoad", "schedule IO"));
        }
        finishOnFailure.release();
        return SaveLoadOperation{std::move(completion)};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "asynchronous load request allocation failed");
    }
}

Core::Result<SaveListOperation> SaveStore::beginList()
{
    if (const auto status = requireAsyncStore(*state_); !status)
    {
        return Core::failure(status.error());
    }
    if (!state_->tryBeginTransaction())
    {
        return Core::failure(SaveErrorCode::Busy,
                             "SaveStore already has an active transaction");
    }
    auto finishOnFailure = Core::makeScopeExit([state = state_]() noexcept {
        state->finishTransaction();
    });

    try
    {
        auto completion = std::make_shared<
            Detail::SaveAsyncState<std::vector<SaveSlotSummary>>>();
        auto scheduled = scheduleOperation<std::vector<SaveSlotSummary>>(
            state_, completion,
            [](Detail::SaveStoreState& state) { return performList(state); });
        if (!scheduled)
        {
            return Core::failure(std::move(scheduled.error()).withContext(
                "SaveStore::beginList", "schedule IO"));
        }
        finishOnFailure.release();
        return SaveListOperation{std::move(completion)};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "asynchronous list request allocation failed");
    }
}

} // namespace Tina::Save
