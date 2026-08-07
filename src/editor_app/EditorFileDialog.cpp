#include "EditorFileDialog.hpp"

#include <tina/core/text/Utf8.hpp>

#include <new>
#include <utility>

#if defined(__linux__)
#include "EditorFileDialogLinux.hpp"
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShObjIdl.h>
#include <objbase.h>

#include <limits>
#include <vector>
#endif

namespace Tina::EditorApp::Detail {
namespace {

#if defined(_WIN32)

[[nodiscard]] Core::Error allocationError(std::string_view operation)
{
    return Core::Error{Core::CoreErrorCode::OutOfMemory, operation};
}

template <typename Interface>
class ComPtr final {
public:
    ComPtr() noexcept = default;

    ~ComPtr() noexcept
    {
        reset();
    }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    [[nodiscard]] Interface* get() const noexcept
    {
        return value_;
    }

    [[nodiscard]] Interface** put() noexcept
    {
        reset();
        return &value_;
    }

    [[nodiscard]] Interface* operator->() const noexcept
    {
        return value_;
    }

    void reset() noexcept
    {
        if (value_ != nullptr) {
            value_->Release();
            value_ = nullptr;
        }
    }

private:
    Interface* value_ = nullptr;
};

class CoTaskMemString final {
public:
    CoTaskMemString() noexcept = default;

    ~CoTaskMemString() noexcept
    {
        if (value_ != nullptr) {
            ::CoTaskMemFree(value_);
        }
    }

    CoTaskMemString(const CoTaskMemString&) = delete;
    CoTaskMemString& operator=(const CoTaskMemString&) = delete;

    [[nodiscard]] PWSTR* put() noexcept
    {
        return &value_;
    }

    [[nodiscard]] PCWSTR get() const noexcept
    {
        return value_;
    }

private:
    PWSTR value_ = nullptr;
};

class ComApartment final {
public:
    ComApartment() noexcept
        : result_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
    {
    }

    ~ComApartment() noexcept
    {
        if (SUCCEEDED(result_)) {
            ::CoUninitialize();
        }
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    [[nodiscard]] HRESULT result() const noexcept
    {
        return result_;
    }

private:
    HRESULT result_ = E_FAIL;
};

[[nodiscard]] Core::ErrorCode errorCodeForHresult(HRESULT result) noexcept
{
    if (result == E_INVALIDARG || result == RPC_E_CHANGED_MODE) {
        return Core::CoreErrorCode::InvalidArgument;
    }
    if (result == E_ACCESSDENIED) {
        return Core::CoreErrorCode::PermissionDenied;
    }
    if (result == E_OUTOFMEMORY) {
        return Core::CoreErrorCode::OutOfMemory;
    }
    return Core::CoreErrorCode::Io;
}

[[nodiscard]] Core::Error hresultError(std::string_view message, HRESULT result)
{
    Core::Error error{errorCodeForHresult(result), message};
    error.setNativeCode(static_cast<Core::i64>(result));
    return error;
}

[[nodiscard]] Core::Result<std::wstring> wideFromUtf8(std::string_view utf8)
{
    if (!Core::isStrictUtf8WithoutNul(utf8) ||
        utf8.size() > static_cast<usize>((std::numeric_limits<int>::max)())) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor file dialog text must be strict UTF-8 without NUL");
    }
    if (utf8.empty()) {
        return std::wstring{};
    }

    const int sourceLength = static_cast<int>(utf8.size());
    const int wideLength = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                                  sourceLength, nullptr, 0);
    if (wideLength <= 0) {
        return Core::failure(hresultError("Editor file dialog UTF-8 conversion failed",
                                          HRESULT_FROM_WIN32(::GetLastError())));
    }
    try {
        std::wstring wide(static_cast<usize>(wideLength), L'\0');
        if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), sourceLength,
                                  wide.data(), wideLength) != wideLength) {
            return Core::failure(hresultError("Editor file dialog UTF-8 conversion failed",
                                              HRESULT_FROM_WIN32(::GetLastError())));
        }
        return wide;
    } catch (const std::bad_alloc&) {
        return Core::failure(allocationError("Editor file dialog UTF-16 allocation failed"));
    }
}

[[nodiscard]] Core::Result<std::string> utf8FromWide(PCWSTR wide)
{
    if (wide == nullptr) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor file dialog returned a null filesystem path");
    }
    const usize wideLength = std::char_traits<wchar_t>::length(wide);
    if (wideLength > static_cast<usize>((std::numeric_limits<int>::max)())) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor file dialog path exceeds the UTF-8 conversion limit");
    }
    if (wideLength == 0U) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor file dialog returned an empty filesystem path");
    }

    const int sourceLength = static_cast<int>(wideLength);
    const int utf8Length = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
                                                  sourceLength, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) {
        return Core::failure(hresultError("Editor file dialog path conversion failed",
                                          HRESULT_FROM_WIN32(::GetLastError())));
    }
    try {
        std::string utf8(static_cast<usize>(utf8Length), '\0');
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, sourceLength,
                                  utf8.data(), utf8Length, nullptr, nullptr) != utf8Length ||
            !Core::isStrictUtf8WithoutNul(utf8)) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Editor file dialog returned a non-UTF-8 filesystem path");
        }
        return utf8;
    } catch (const std::bad_alloc&) {
        return Core::failure(allocationError("Editor file dialog path allocation failed"));
    }
}

[[nodiscard]] Core::Status setDialogTitle(IFileDialog& dialog, std::string_view titleUtf8)
{
    if (titleUtf8.empty()) {
        return Core::success();
    }
    auto title = wideFromUtf8(titleUtf8);
    if (!title) {
        return Core::failure(std::move(title.error()));
    }
    const HRESULT result = dialog.SetTitle(title->c_str());
    return SUCCEEDED(result)
               ? Core::success()
               : Core::failure(hresultError("Editor file dialog title could not be set", result));
}

[[nodiscard]] Core::Status setInitialDirectory(IFileDialog& dialog,
                                               std::string_view directoryUtf8)
{
    if (directoryUtf8.empty()) {
        return Core::success();
    }
    auto directory = wideFromUtf8(directoryUtf8);
    if (!directory) {
        return Core::failure(std::move(directory.error()));
    }
    ComPtr<IShellItem> folder;
    const HRESULT createResult = ::SHCreateItemFromParsingName(directory->c_str(), nullptr,
                                                               IID_PPV_ARGS(folder.put()));
    if (FAILED(createResult)) {
        return Core::failure(hresultError(
            "Editor file dialog initial directory could not be opened", createResult));
    }
    const HRESULT setResult = dialog.SetDefaultFolder(folder.get());
    return SUCCEEDED(setResult)
               ? Core::success()
               : Core::failure(hresultError(
                     "Editor file dialog initial directory could not be selected", setResult));
}

struct NativeDialogFilters final {
    std::vector<std::wstring> labels;
    std::vector<std::wstring> patterns;
    std::vector<COMDLG_FILTERSPEC> specs;
};

[[nodiscard]] Core::Result<NativeDialogFilters>
makeNativeFilters(std::span<const EditorFileDialogFilter> filters)
{
    if (filters.size() > static_cast<usize>((std::numeric_limits<UINT>::max)())) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor file dialog has too many filters");
    }
    try {
        NativeDialogFilters output{};
        output.labels.reserve(filters.size());
        output.patterns.reserve(filters.size());
        output.specs.reserve(filters.size());
        for (const EditorFileDialogFilter& filter : filters) {
            if (filter.labelUtf8.empty() || filter.patternUtf8.empty()) {
                return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                     "Editor file dialog filters require a label and pattern");
            }
            auto label = wideFromUtf8(filter.labelUtf8);
            if (!label) {
                return Core::failure(std::move(label.error()));
            }
            auto pattern = wideFromUtf8(filter.patternUtf8);
            if (!pattern) {
                return Core::failure(std::move(pattern.error()));
            }
            output.labels.push_back(std::move(*label));
            output.patterns.push_back(std::move(*pattern));
        }
        for (usize index = 0; index < output.labels.size(); ++index) {
            output.specs.push_back(COMDLG_FILTERSPEC{
                .pszName = output.labels[index].c_str(),
                .pszSpec = output.patterns[index].c_str(),
            });
        }
        return output;
    } catch (const std::bad_alloc&) {
        return Core::failure(allocationError("Editor file dialog filter allocation failed"));
    }
}

[[nodiscard]] Core::Result<EditorFileDialogResult>
showDialog(IFileDialog& dialog, uintptr nativeOwnerWindow)
{
    HWND ownerWindow = reinterpret_cast<HWND>(nativeOwnerWindow);
    if (ownerWindow == nullptr) {
        ownerWindow = ::GetActiveWindow();
    }
    const HRESULT showResult = dialog.Show(ownerWindow);
    if (showResult == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return EditorFileDialogResult{};
    }
    if (FAILED(showResult)) {
        return Core::failure(hresultError("Editor file dialog could not be shown", showResult));
    }

    ComPtr<IShellItem> selection;
    const HRESULT selectionResult = dialog.GetResult(selection.put());
    if (FAILED(selectionResult)) {
        return Core::failure(hresultError("Editor file dialog selection could not be read",
                                          selectionResult));
    }
    CoTaskMemString path;
    const HRESULT pathResult = selection->GetDisplayName(SIGDN_FILESYSPATH, path.put());
    if (FAILED(pathResult)) {
        return Core::failure(hresultError(
            "Editor file dialog selection is not a filesystem path", pathResult));
    }
    auto pathUtf8 = utf8FromWide(path.get());
    if (!pathUtf8) {
        return Core::failure(std::move(pathUtf8.error()));
    }
    return EditorFileDialogResult{
        .outcome = EditorFileDialogOutcome::Selected,
        .selectedPathUtf8 = std::move(*pathUtf8),
    };
}

#endif

} // namespace

EditorFileDialog::EditorFileDialog(uintptr nativeOwnerWindow) noexcept
    : ownerThread_(std::this_thread::get_id()), nativeOwnerWindow_(nativeOwnerWindow)
{
}

Core::Status EditorFileDialog::ensureOwnerThread() const
{
    if (std::this_thread::get_id() != ownerThread_) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor file dialog must run on its owner thread");
    }
    return Core::success();
}

Core::Result<EditorFileDialogResult>
EditorFileDialog::openExistingFile(const OpenExistingFileDialogRequest& request) const
{
#if defined(_WIN32)
    if (auto status = ensureOwnerThread(); !status) {
        return Core::failure(std::move(status.error()));
    }
    ComApartment apartment;
    if (FAILED(apartment.result())) {
        return Core::failure(hresultError("Editor file dialog requires a Windows STA thread",
                                          apartment.result()));
    }
    ComPtr<IFileOpenDialog> dialog;
    const HRESULT createResult = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                                     CLSCTX_INPROC_SERVER,
                                                     IID_PPV_ARGS(dialog.put()));
    if (FAILED(createResult)) {
        return Core::failure(hresultError("Editor open-file dialog could not be created",
                                          createResult));
    }
    FILEOPENDIALOGOPTIONS options = 0;
    if (const HRESULT result = dialog->GetOptions(&options); FAILED(result)) {
        return Core::failure(hresultError("Editor open-file dialog options could not be read", result));
    }
    options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    if (const HRESULT result = dialog->SetOptions(options); FAILED(result)) {
        return Core::failure(hresultError("Editor open-file dialog options could not be set", result));
    }
    if (auto status = setDialogTitle(*dialog.get(), request.titleUtf8); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = setInitialDirectory(*dialog.get(), request.initialDirectoryUtf8); !status) {
        return Core::failure(std::move(status.error()));
    }
    auto nativeFilters = makeNativeFilters(request.filters);
    if (!nativeFilters) {
        return Core::failure(std::move(nativeFilters.error()));
    }
    if (!nativeFilters->specs.empty()) {
        const HRESULT result = dialog->SetFileTypes(
            static_cast<UINT>(nativeFilters->specs.size()), nativeFilters->specs.data());
        if (FAILED(result)) {
            return Core::failure(hresultError(
                "Editor open-file dialog filters could not be set", result));
        }
    }
    return showDialog(*dialog.get(), nativeOwnerWindow_);
#elif defined(__linux__)
    if (auto status = ensureOwnerThread(); !status) {
        return Core::failure(std::move(status.error()));
    }
    return openExistingFileLinux(request);
#else
    static_cast<void>(request);
    return Core::failure(Core::CoreErrorCode::Unsupported,
                         "Editor native file dialogs are not available on this platform");
#endif
}

Core::Result<EditorFileDialogResult>
EditorFileDialog::saveFile(const SaveFileDialogRequest& request) const
{
#if defined(_WIN32)
    if (auto status = ensureOwnerThread(); !status) {
        return Core::failure(std::move(status.error()));
    }
    ComApartment apartment;
    if (FAILED(apartment.result())) {
        return Core::failure(hresultError("Editor file dialog requires a Windows STA thread",
                                          apartment.result()));
    }
    ComPtr<IFileSaveDialog> dialog;
    const HRESULT createResult = ::CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                                                     CLSCTX_INPROC_SERVER,
                                                     IID_PPV_ARGS(dialog.put()));
    if (FAILED(createResult)) {
        return Core::failure(hresultError("Editor save-file dialog could not be created",
                                          createResult));
    }
    FILEOPENDIALOGOPTIONS options = 0;
    if (const HRESULT result = dialog->GetOptions(&options); FAILED(result)) {
        return Core::failure(hresultError("Editor save-file dialog options could not be read", result));
    }
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT | FOS_NOCHANGEDIR;
    if (const HRESULT result = dialog->SetOptions(options); FAILED(result)) {
        return Core::failure(hresultError("Editor save-file dialog options could not be set", result));
    }
    if (auto status = setDialogTitle(*dialog.get(), request.titleUtf8); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = setInitialDirectory(*dialog.get(), request.initialDirectoryUtf8); !status) {
        return Core::failure(std::move(status.error()));
    }
    auto nativeFilters = makeNativeFilters(request.filters);
    if (!nativeFilters) {
        return Core::failure(std::move(nativeFilters.error()));
    }
    if (!nativeFilters->specs.empty()) {
        const HRESULT result = dialog->SetFileTypes(
            static_cast<UINT>(nativeFilters->specs.size()), nativeFilters->specs.data());
        if (FAILED(result)) {
            return Core::failure(hresultError(
                "Editor save-file dialog filters could not be set", result));
        }
    }
    if (!request.suggestedFileNameUtf8.empty()) {
        auto name = wideFromUtf8(request.suggestedFileNameUtf8);
        if (!name) {
            return Core::failure(std::move(name.error()));
        }
        if (const HRESULT result = dialog->SetFileName(name->c_str()); FAILED(result)) {
            return Core::failure(hresultError("Editor save-file name could not be set", result));
        }
    }
    if (!request.defaultExtensionUtf8.empty()) {
        if (request.defaultExtensionUtf8.starts_with('.')) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Editor save-file default extension must not start with a dot");
        }
        auto extension = wideFromUtf8(request.defaultExtensionUtf8);
        if (!extension) {
            return Core::failure(std::move(extension.error()));
        }
        if (const HRESULT result = dialog->SetDefaultExtension(extension->c_str()); FAILED(result)) {
            return Core::failure(hresultError("Editor save-file extension could not be set", result));
        }
    }
    return showDialog(*dialog.get(), nativeOwnerWindow_);
#elif defined(__linux__)
    if (auto status = ensureOwnerThread(); !status) {
        return Core::failure(std::move(status.error()));
    }
    return saveFileLinux(request);
#else
    static_cast<void>(request);
    return Core::failure(Core::CoreErrorCode::Unsupported,
                         "Editor native file dialogs are not available on this platform");
#endif
}

Core::Result<EditorFileDialogResult>
EditorFileDialog::pickFolder(const PickFolderDialogRequest& request) const
{
#if defined(_WIN32)
    if (auto status = ensureOwnerThread(); !status) {
        return Core::failure(std::move(status.error()));
    }
    ComApartment apartment;
    if (FAILED(apartment.result())) {
        return Core::failure(hresultError("Editor file dialog requires a Windows STA thread",
                                          apartment.result()));
    }
    ComPtr<IFileOpenDialog> dialog;
    const HRESULT createResult = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                                     CLSCTX_INPROC_SERVER,
                                                     IID_PPV_ARGS(dialog.put()));
    if (FAILED(createResult)) {
        return Core::failure(hresultError("Editor folder dialog could not be created", createResult));
    }
    FILEOPENDIALOGOPTIONS options = 0;
    if (const HRESULT result = dialog->GetOptions(&options); FAILED(result)) {
        return Core::failure(hresultError("Editor folder dialog options could not be read", result));
    }
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_PICKFOLDERS | FOS_NOCHANGEDIR;
    if (const HRESULT result = dialog->SetOptions(options); FAILED(result)) {
        return Core::failure(hresultError("Editor folder dialog options could not be set", result));
    }
    if (auto status = setDialogTitle(*dialog.get(), request.titleUtf8); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = setInitialDirectory(*dialog.get(), request.initialDirectoryUtf8); !status) {
        return Core::failure(std::move(status.error()));
    }
    return showDialog(*dialog.get(), nativeOwnerWindow_);
#elif defined(__linux__)
    if (auto status = ensureOwnerThread(); !status) {
        return Core::failure(std::move(status.error()));
    }
    return pickFolderLinux(request);
#else
    static_cast<void>(request);
    return Core::failure(Core::CoreErrorCode::Unsupported,
                         "Editor native file dialogs are not available on this platform");
#endif
}

} // namespace Tina::EditorApp::Detail
