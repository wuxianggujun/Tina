#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/save/SaveTypes.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Save {

namespace Detail {
template <typename Value>
class SaveAsyncState;
class SaveStoreState;
} // namespace Detail

// Resolves <per-user state>/<applicationName>/saves without touching the filesystem.
// applicationName is validated by Core::userApplicationDirectory as one safe path segment.
[[nodiscard]] Core::Result<std::string> defaultSaveRootPath(std::string_view applicationName);

class SaveWriteOperation final {
  public:
    SaveWriteOperation() noexcept = default;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    // Thread-safe and one-shot. Calling before ready or more than once fails explicitly.
    [[nodiscard]] Core::Result<SaveWriteResult> take();

  private:
    friend class SaveStore;
    explicit SaveWriteOperation(std::shared_ptr<Detail::SaveAsyncState<SaveWriteResult>> state) noexcept;

    std::shared_ptr<Detail::SaveAsyncState<SaveWriteResult>> state_{};
};

class SaveLoadOperation final {
  public:
    SaveLoadOperation() noexcept = default;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] Core::Result<SaveLoadResult> take();

  private:
    friend class SaveStore;
    explicit SaveLoadOperation(std::shared_ptr<Detail::SaveAsyncState<SaveLoadResult>> state) noexcept;

    std::shared_ptr<Detail::SaveAsyncState<SaveLoadResult>> state_{};
};

class SaveListOperation final {
  public:
    SaveListOperation() noexcept = default;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] Core::Result<std::vector<SaveSlotSummary>> take();

  private:
    friend class SaveStore;
    explicit SaveListOperation(
        std::shared_ptr<Detail::SaveAsyncState<std::vector<SaveSlotSummary>>> state) noexcept;

    std::shared_ptr<Detail::SaveAsyncState<std::vector<SaveSlotSummary>>> state_{};
};

// Owner-thread command surface for versioned save slots. At most one sync or
// async filesystem transaction may be active per store. Async operation handles
// remain valid if the SaveStore facade is destroyed; the configured TaskSystem
// must outlive every call that schedules work.
class SaveStore final {
  public:
    SaveStore() = delete;
    ~SaveStore() noexcept;

    SaveStore(const SaveStore&) = delete;
    SaveStore& operator=(const SaveStore&) = delete;
    SaveStore(SaveStore&&) = delete;
    SaveStore& operator=(SaveStore&&) = delete;

    [[nodiscard]] static Core::Result<std::unique_ptr<SaveStore>> Create(SaveStoreConfig config);

    // Pure path composition; does not access disk and may be queried from any thread.
    [[nodiscard]] Core::Result<SaveStorePaths> pathsFor(SaveSlotId slot) const;
    [[nodiscard]] bool isBusy() const noexcept;

    [[nodiscard]] Core::Result<SaveWriteResult> saveSlot(const SaveWriteRequest& request);
    [[nodiscard]] Core::Result<SaveLoadResult> loadSlot(
        SaveSlotId slot, SaveLoadOptions options = {});
    [[nodiscard]] Core::Result<std::vector<SaveSlotSummary>> listSlots();
    // Idempotent. Backup is deleted first so a primary delete failure leaves a loadable copy.
    [[nodiscard]] Core::Result<SaveDeleteResult> deleteSlot(SaveSlotId slot);
    // Never runs implicitly during load. A valid primary is left untouched.
    [[nodiscard]] Core::Result<SaveRepairResult> repairPrimaryFromBackup(SaveSlotId slot);

    // Request payload and metadata are copied before work is scheduled. Completion
    // is observed through the returned operation; no main-thread queue is required.
    [[nodiscard]] Core::Result<SaveWriteOperation> beginSave(const SaveWriteRequest& request);
    [[nodiscard]] Core::Result<SaveLoadOperation> beginLoad(
        SaveSlotId slot, SaveLoadOptions options = {});
    [[nodiscard]] Core::Result<SaveListOperation> beginList();

  private:
    explicit SaveStore(std::shared_ptr<Detail::SaveStoreState> state) noexcept;

    std::shared_ptr<Detail::SaveStoreState> state_;
};

} // namespace Tina::Save
