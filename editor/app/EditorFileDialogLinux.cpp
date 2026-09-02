#include "EditorFileDialogLinux.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/text/Utf8.hpp>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <new>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern char** environ;

namespace Tina::EditorApp::Detail {
namespace {

constexpr usize MaxCapturedOutputBytes = 1024U * 1024U;
constexpr int TerminationPollMilliseconds = 10;
constexpr u32 TerminationPollAttempts = 50U;

enum class DialogKind : u8 {
    OpenFile,
    SaveFile,
    PickFolder,
};

class FileDescriptor final {
public:
    FileDescriptor() noexcept = default;

    explicit FileDescriptor(int value) noexcept
        : value_(value)
    {
    }

    ~FileDescriptor() noexcept
    {
        reset();
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept
        : value_(std::exchange(other.value_, -1))
    {
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) {
            reset(std::exchange(other.value_, -1));
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept
    {
        return value_;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return value_ >= 0;
    }

    void reset(int replacement = -1) noexcept
    {
        if (value_ >= 0) {
            static_cast<void>(::close(value_));
        }
        value_ = replacement;
    }

private:
    int value_ = -1;
};

struct ProcessPipe final {
    FileDescriptor readEnd{};
    FileDescriptor writeEnd{};
};

struct ProcessResult final {
    int status = 0;
    std::string standardOutput{};
    std::string standardError{};
};

[[nodiscard]] Core::ErrorCode errorCodeForErrno(int nativeCode) noexcept
{
    switch (nativeCode) {
    case EINVAL:
        return Core::CoreErrorCode::InvalidArgument;
    case ENOENT:
        return Core::CoreErrorCode::NotFound;
    case EACCES:
    case EPERM:
        return Core::CoreErrorCode::PermissionDenied;
    case ENOMEM:
        return Core::CoreErrorCode::OutOfMemory;
    default:
        return Core::CoreErrorCode::Io;
    }
}

[[nodiscard]] Core::Error posixError(std::string_view message, int nativeCode)
{
    Core::Error error{errorCodeForErrno(nativeCode), message};
    error.setNativeCode(static_cast<i64>(nativeCode));
    return error;
}

[[nodiscard]] Core::Error processExitError(std::string_view executable,
                                           const ProcessResult& process)
{
    std::string message = "Linux editor file dialog helper failed: ";
    message.append(executable);

    std::string_view diagnostic = process.standardError;
    while (!diagnostic.empty() &&
           (diagnostic.back() == '\n' || diagnostic.back() == '\r')) {
        diagnostic.remove_suffix(1U);
    }
    constexpr usize MaxDiagnosticBytes = 4096U;
    if (diagnostic.size() > MaxDiagnosticBytes) {
        diagnostic = diagnostic.substr(0U, MaxDiagnosticBytes);
    }
    if (!diagnostic.empty() && Core::isStrictUtf8WithoutNul(diagnostic)) {
        message.append(": ");
        message.append(diagnostic);
    }

    Core::Error error{Core::CoreErrorCode::Io, message};
    if (WIFEXITED(process.status)) {
        error.setNativeCode(static_cast<i64>(WEXITSTATUS(process.status)));
    } else if (WIFSIGNALED(process.status)) {
        error.setNativeCode(static_cast<i64>(128 + WTERMSIG(process.status)));
    }
    return error;
}

[[nodiscard]] Core::Status ensureAboveStandardStreams(FileDescriptor& descriptor)
{
    if (descriptor.get() > STDERR_FILENO) {
        return Core::success();
    }
    const int replacement = ::fcntl(descriptor.get(), F_DUPFD_CLOEXEC,
                                    STDERR_FILENO + 1);
    if (replacement < 0) {
        return Core::failure(posixError(
            "Linux editor file dialog pipe descriptor could not be relocated", errno));
    }
    descriptor.reset(replacement);
    return Core::success();
}

[[nodiscard]] Core::Result<ProcessPipe> makeProcessPipe()
{
    int descriptors[2] = {-1, -1};
    if (::pipe(descriptors) != 0) {
        return Core::failure(posixError("Linux editor file dialog pipe could not be created",
                                        errno));
    }

    ProcessPipe pipe{
        .readEnd = FileDescriptor{descriptors[0]},
        .writeEnd = FileDescriptor{descriptors[1]},
    };
    if (auto status = ensureAboveStandardStreams(pipe.readEnd); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = ensureAboveStandardStreams(pipe.writeEnd); !status) {
        return Core::failure(std::move(status.error()));
    }
    const int relocatedDescriptors[] = {pipe.readEnd.get(), pipe.writeEnd.get()};
    for (const int descriptor : relocatedDescriptors) {
        const int flags = ::fcntl(descriptor, F_GETFD);
        if (flags < 0 || ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) != 0) {
            return Core::failure(posixError(
                "Linux editor file dialog pipe could not be made close-on-exec", errno));
        }
    }

    const int readFlags = ::fcntl(pipe.readEnd.get(), F_GETFL);
    if (readFlags < 0 ||
        ::fcntl(pipe.readEnd.get(), F_SETFL, readFlags | O_NONBLOCK) != 0) {
        return Core::failure(posixError(
            "Linux editor file dialog pipe could not be made non-blocking", errno));
    }
    return pipe;
}

[[nodiscard]] Core::Status addSpawnFileAction(int result, std::string_view operation)
{
    if (result == 0) {
        return Core::success();
    }
    return Core::failure(posixError(operation, result));
}

enum class ChildWaitState : u8 {
    Running,
    Reaped,
    Failed,
};

[[nodiscard]] ChildWaitState tryReapChild(pid_t processId) noexcept
{
    int status = 0;
    while (true) {
        const pid_t waitResult = ::waitpid(processId, &status, WNOHANG);
        if (waitResult == processId) {
            return ChildWaitState::Reaped;
        }
        if (waitResult == 0) {
            return ChildWaitState::Running;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == ECHILD) {
            return ChildWaitState::Reaped;
        }
        return ChildWaitState::Failed;
    }
}

[[nodiscard]] bool waitForChildExit(pid_t processId) noexcept
{
    for (u32 attempt = 0; attempt < TerminationPollAttempts; ++attempt) {
        const ChildWaitState state = tryReapChild(processId);
        if (state == ChildWaitState::Reaped) {
            return true;
        }
        if (state == ChildWaitState::Failed) {
            return false;
        }
        while (::poll(nullptr, 0, TerminationPollMilliseconds) < 0 && errno == EINTR) {
        }
    }
    return false;
}

void reapChildBlocking(pid_t processId) noexcept
{
    int status = 0;
    while (true) {
        const pid_t waitResult = ::waitpid(processId, &status, 0);
        if (waitResult == processId) {
            return;
        }
        if (waitResult < 0 && errno == EINTR) {
            continue;
        }
        if (waitResult < 0 && errno == ECHILD) {
            return;
        }
        return;
    }
}

void terminateAndWait(pid_t processId) noexcept
{
    if (tryReapChild(processId) == ChildWaitState::Reaped) {
        return;
    }
    if (::kill(processId, SIGTERM) == 0 || errno != ESRCH) {
        if (waitForChildExit(processId)) {
            return;
        }
    } else if (tryReapChild(processId) == ChildWaitState::Reaped) {
        return;
    }

    if (::kill(processId, SIGKILL) != 0 && errno == ESRCH &&
        tryReapChild(processId) == ChildWaitState::Reaped) {
        return;
    }
    reapChildBlocking(processId);
}

[[nodiscard]] Core::Status appendPipeOutput(FileDescriptor& descriptor,
                                            std::string& output,
                                            bool& outputTooLarge,
                                            bool hangup)
{
    char buffer[4096];
    while (descriptor.valid()) {
        const ssize_t count = ::read(descriptor.get(), buffer, sizeof(buffer));
        if (count > 0) {
            const usize byteCount = static_cast<usize>(count);
            if (output.size() <= MaxCapturedOutputBytes &&
                byteCount <= MaxCapturedOutputBytes - output.size()) {
                output.append(buffer, byteCount);
            } else {
                outputTooLarge = true;
            }
            continue;
        }
        if (count == 0) {
            descriptor.reset();
            return Core::success();
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (hangup) {
                descriptor.reset();
            }
            return Core::success();
        }
        const int nativeCode = errno;
        descriptor.reset();
        return Core::failure(posixError(
            "Linux editor file dialog process output could not be read", nativeCode));
    }
    return Core::success();
}

[[nodiscard]] Core::Status captureProcessOutput(ProcessPipe& standardOutput,
                                                ProcessPipe& standardError,
                                                std::string& output,
                                                std::string& errorOutput)
{
    bool outputTooLarge = false;
    bool errorTooLarge = false;
    while (standardOutput.readEnd.valid() || standardError.readEnd.valid()) {
        pollfd descriptors[2] = {
            pollfd{standardOutput.readEnd.get(), POLLIN, 0},
            pollfd{standardError.readEnd.get(), POLLIN, 0},
        };
        const int pollResult = ::poll(descriptors, 2, -1);
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Core::failure(posixError(
                "Linux editor file dialog process output could not be polled", errno));
        }

        if (standardOutput.readEnd.valid() && descriptors[0].revents != 0) {
            const bool hangup = (descriptors[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
            if (auto status = appendPipeOutput(standardOutput.readEnd, output,
                                               outputTooLarge, hangup);
                !status) {
                return status;
            }
        }
        if (standardError.readEnd.valid() && descriptors[1].revents != 0) {
            const bool hangup = (descriptors[1].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
            if (auto status = appendPipeOutput(standardError.readEnd, errorOutput,
                                               errorTooLarge, hangup);
                !status) {
                return status;
            }
        }
    }

    if (outputTooLarge || errorTooLarge) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Linux editor file dialog helper output exceeded 1 MiB");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<ProcessResult>
runProcess(const std::vector<std::string>& arguments)
{
    if (arguments.empty()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Linux editor file dialog helper command is empty");
    }

    auto standardOutput = makeProcessPipe();
    if (!standardOutput) {
        return Core::failure(std::move(standardOutput.error()));
    }
    auto standardError = makeProcessPipe();
    if (!standardError) {
        return Core::failure(std::move(standardError.error()));
    }

    posix_spawn_file_actions_t fileActions{};
    const int initializeResult = ::posix_spawn_file_actions_init(&fileActions);
    if (initializeResult != 0) {
        return Core::failure(posixError(
            "Linux editor file dialog spawn actions could not be initialized",
            initializeResult));
    }
    auto destroyFileActions = Core::makeScopeExit([&fileActions]() noexcept {
        static_cast<void>(::posix_spawn_file_actions_destroy(&fileActions));
    });

    const auto addAction = [&](int result, std::string_view operation) -> Core::Status {
        return addSpawnFileAction(result, operation);
    };
    if (auto status = addAction(
            ::posix_spawn_file_actions_adddup2(&fileActions,
                                               standardOutput->writeEnd.get(),
                                               STDOUT_FILENO),
            "Linux editor file dialog stdout redirection could not be configured");
        !status) {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = addAction(
            ::posix_spawn_file_actions_adddup2(&fileActions,
                                               standardError->writeEnd.get(),
                                               STDERR_FILENO),
            "Linux editor file dialog stderr redirection could not be configured");
        !status) {
        return Core::failure(std::move(status.error()));
    }

    const int descriptorsToClose[] = {
        standardOutput->readEnd.get(),
        standardOutput->writeEnd.get(),
        standardError->readEnd.get(),
        standardError->writeEnd.get(),
    };
    for (const int descriptor : descriptorsToClose) {
        if (auto status = addAction(
                ::posix_spawn_file_actions_addclose(&fileActions, descriptor),
                "Linux editor file dialog inherited pipe could not be closed");
            !status) {
            return Core::failure(std::move(status.error()));
        }
    }

    std::vector<char*> nativeArguments;
    nativeArguments.reserve(arguments.size() + 1U);
    for (const std::string& argument : arguments) {
        nativeArguments.push_back(const_cast<char*>(argument.c_str()));
    }
    nativeArguments.push_back(nullptr);

    pid_t processId = 0;
    const int spawnResult = ::posix_spawnp(&processId, nativeArguments.front(),
                                           &fileActions, nullptr,
                                           nativeArguments.data(), environ);
    if (spawnResult != 0) {
        return Core::failure(posixError(
            "Linux editor file dialog helper could not be started", spawnResult));
    }
    auto reapProcess = Core::makeScopeExit([processId]() noexcept {
        terminateAndWait(processId);
    });

    standardOutput->writeEnd.reset();
    standardError->writeEnd.reset();
    ProcessResult process{};
    if (auto status = captureProcessOutput(*standardOutput, *standardError,
                                           process.standardOutput,
                                           process.standardError);
        !status) {
        return Core::failure(std::move(status.error()));
    }

    while (::waitpid(processId, &process.status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return Core::failure(posixError(
            "Linux editor file dialog helper could not be reaped", errno));
    }
    reapProcess.release();
    return process;
}

[[nodiscard]] Core::Status validateText(std::string_view value, std::string_view field)
{
    if (!Core::isStrictUtf8WithoutNul(value) || value.contains('\n') || value.contains('\r')) {
        std::string message = "Linux editor file dialog ";
        message.append(field);
        message.append(" must be strict UTF-8 without NUL or line breaks");
        return Core::failure(Core::CoreErrorCode::InvalidArgument, message);
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateFilters(std::span<const EditorFileDialogFilter> filters)
{
    for (const EditorFileDialogFilter& filter : filters) {
        if (filter.labelUtf8.empty() || filter.patternUtf8.empty()) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Linux editor file dialog filters require a label and pattern");
        }
        if (auto status = validateText(filter.labelUtf8, "filter label"); !status) {
            return status;
        }
        if (auto status = validateText(filter.patternUtf8, "filter pattern"); !status) {
            return status;
        }
        if (filter.labelUtf8.contains('|') || filter.patternUtf8.contains('|')) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Linux editor file dialog filters must not contain '|'");
        }
    }
    return Core::success();
}

[[nodiscard]] std::string normalizedFilterPattern(std::string_view pattern)
{
    std::string normalized;
    normalized.reserve(pattern.size());
    usize tokenBegin = 0;
    while (tokenBegin <= pattern.size()) {
        const usize tokenEnd = pattern.find(';', tokenBegin);
        const std::string_view token = pattern.substr(
            tokenBegin,
            tokenEnd == std::string_view::npos ? std::string_view::npos
                                                : tokenEnd - tokenBegin);
        if (!normalized.empty()) {
            normalized.push_back(' ');
        }
        normalized.append(token == "*.*" ? "*" : token);
        if (tokenEnd == std::string_view::npos) {
            break;
        }
        tokenBegin = tokenEnd + 1U;
    }
    return normalized;
}

[[nodiscard]] std::string zenityInitialSelection(std::string_view initialDirectory,
                                                 std::string_view suggestedFileName,
                                                 DialogKind kind)
{
    std::string selection{initialDirectory};
    if (!selection.empty() && !selection.ends_with('/')) {
        selection.push_back('/');
    }
    if (kind == DialogKind::SaveFile) {
        selection.append(suggestedFileName);
    }
    return selection;
}

[[nodiscard]] std::vector<std::string>
makeZenityArguments(DialogKind kind,
                    std::string_view title,
                    std::string_view initialDirectory,
                    std::string_view suggestedFileName,
                    std::span<const EditorFileDialogFilter> filters)
{
    std::vector<std::string> arguments{"zenity", "--file-selection"};
    if (kind == DialogKind::PickFolder) {
        arguments.emplace_back("--directory");
    } else if (kind == DialogKind::SaveFile) {
        arguments.emplace_back("--save");
        arguments.emplace_back("--confirm-overwrite");
    }
    if (!title.empty()) {
        arguments.emplace_back("--title=" + std::string{title});
    }
    const std::string initialSelection = zenityInitialSelection(
        initialDirectory, suggestedFileName, kind);
    if (!initialSelection.empty()) {
        arguments.emplace_back("--filename=" + initialSelection);
    }
    for (const EditorFileDialogFilter& filter : filters) {
        arguments.emplace_back("--file-filter=" + std::string{filter.labelUtf8} + " | " +
                               normalizedFilterPattern(filter.patternUtf8));
    }
    return arguments;
}

[[nodiscard]] std::string
kdialogFilter(std::span<const EditorFileDialogFilter> filters)
{
    std::string output;
    for (const EditorFileDialogFilter& filter : filters) {
        if (!output.empty()) {
            output.push_back('\n');
        }
        output.append(normalizedFilterPattern(filter.patternUtf8));
        output.push_back('|');
        output.append(filter.labelUtf8);
    }
    return output;
}

[[nodiscard]] std::vector<std::string>
makeKdialogArguments(DialogKind kind,
                     std::string_view title,
                     std::string_view initialDirectory,
                     std::string_view suggestedFileName,
                     std::span<const EditorFileDialogFilter> filters)
{
    std::vector<std::string> arguments{"kdialog"};
    if (!title.empty()) {
        arguments.emplace_back("--title");
        arguments.emplace_back(title);
    }
    switch (kind) {
    case DialogKind::OpenFile:
        arguments.emplace_back("--getopenfilename");
        break;
    case DialogKind::SaveFile:
        arguments.emplace_back("--getsavefilename");
        break;
    case DialogKind::PickFolder:
        arguments.emplace_back("--getexistingdirectory");
        break;
    }

    std::string initialSelection{initialDirectory};
    if (kind == DialogKind::SaveFile && !suggestedFileName.empty()) {
        if (!initialSelection.empty() && !initialSelection.ends_with('/')) {
            initialSelection.push_back('/');
        }
        initialSelection.append(suggestedFileName);
    }
    if (!initialSelection.empty()) {
        arguments.push_back(std::move(initialSelection));
    } else if (!filters.empty() && kind != DialogKind::PickFolder) {
        arguments.emplace_back(".");
    }
    if (!filters.empty() && kind != DialogKind::PickFolder) {
        arguments.push_back(kdialogFilter(filters));
    }
    return arguments;
}

[[nodiscard]] Core::Result<std::string> selectedPathFromOutput(std::string output)
{
    if (!output.empty() && output.back() == '\n') {
        output.pop_back();
        if (!output.empty() && output.back() == '\r') {
            output.pop_back();
        }
    } else if (!output.empty() && output.back() == '\r') {
        output.pop_back();
    }
    if (output.empty()) {
        return Core::failure(Core::CoreErrorCode::Io,
                             "Linux editor file dialog returned an empty path");
    }
    if (!Core::isStrictUtf8WithoutNul(output) || output.contains('\n') || output.contains('\r')) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Linux editor file dialog returned a non-UTF-8 filesystem path");
    }
    if (!std::filesystem::path{std::string{output}}.is_absolute()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Linux editor file dialog returned a relative filesystem path");
    }
    return output;
}

[[nodiscard]] bool appendDefaultExtension(std::string& path, std::string_view extension)
{
    if (extension.empty()) {
        return false;
    }
    const usize separator = path.find_last_of('/');
    const usize fileNameBegin = separator == std::string::npos ? 0U : separator + 1U;
    if (path.find('.', fileNameBegin) == std::string::npos) {
        path.push_back('.');
        path.append(extension);
        return true;
    }
    return false;
}

[[nodiscard]] Core::Status validateSelectedPath(std::string_view path, DialogKind kind)
{
    std::error_code error;
    const std::filesystem::path nativePath{std::string{path}};
    if (kind == DialogKind::OpenFile) {
        const bool exists = std::filesystem::exists(nativePath, error);
        const bool directory = !error && std::filesystem::is_directory(nativePath, error);
        if (error) {
            return Core::failure(Core::CoreErrorCode::Io,
                                 "Linux editor open-file selection could not be inspected");
        }
        if (!exists || directory) {
            return Core::failure(Core::CoreErrorCode::NotFound,
                                 "Linux editor open-file selection is not an existing file");
        }
    } else if (kind == DialogKind::PickFolder) {
        const bool directory = std::filesystem::is_directory(nativePath, error);
        if (error) {
            return Core::failure(Core::CoreErrorCode::Io,
                                 "Linux editor folder selection could not be inspected");
        }
        if (!directory) {
            return Core::failure(Core::CoreErrorCode::NotFound,
                                 "Linux editor folder selection is not an existing directory");
        }
    } else {
        const std::string fileName = nativePath.filename().generic_string();
        if (fileName.empty() || fileName == "." || fileName == "..") {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Linux editor save-file selection must include a file name");
        }
        const bool selectedDirectory = std::filesystem::is_directory(nativePath, error);
        if (error) {
            return Core::failure(Core::CoreErrorCode::Io,
                                 "Linux editor save-file selection could not be inspected");
        }
        if (selectedDirectory) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Linux editor save-file selection must not be a directory");
        }
        std::filesystem::path parent = nativePath.parent_path();
        if (parent.empty()) {
            parent = "/";
        }
        error.clear();
        const bool directory = std::filesystem::is_directory(parent, error);
        if (error) {
            return Core::failure(Core::CoreErrorCode::Io,
                                 "Linux editor save-file parent directory could not be inspected");
        }
        if (!directory) {
            return Core::failure(Core::CoreErrorCode::NotFound,
                                 "Linux editor save-file parent is not an existing directory");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Result<EditorFileDialogResult>
showLinuxDialog(DialogKind kind,
                std::string_view title,
                std::string_view initialDirectory,
                std::string_view suggestedFileName,
                std::string_view defaultExtension,
                std::span<const EditorFileDialogFilter> filters)
{
    const std::vector<std::vector<std::string>> helperArguments{
        makeZenityArguments(kind, title, initialDirectory, suggestedFileName, filters),
        makeKdialogArguments(kind, title, initialDirectory, suggestedFileName, filters),
    };
    for (const std::vector<std::string>& arguments : helperArguments) {
        auto process = runProcess(arguments);
        if (!process) {
            if (process.error().code == Core::CoreErrorCode::NotFound) {
                continue;
            }
            return Core::failure(std::move(process.error()));
        }
        if (WIFEXITED(process->status) && WEXITSTATUS(process->status) == 1 &&
            process->standardError.empty()) {
            return EditorFileDialogResult{};
        }
        if (!WIFEXITED(process->status) || WEXITSTATUS(process->status) != 0) {
            return Core::failure(processExitError(arguments.front(), *process));
        }

        auto selectedPath = selectedPathFromOutput(std::move(process->standardOutput));
        if (!selectedPath) {
            return Core::failure(std::move(selectedPath.error()));
        }
        // Validate the helper result before adding a default extension. A
        // directory such as "/" must not become "/.tworld".
        if (auto status = validateSelectedPath(*selectedPath, kind); !status) {
            return Core::failure(std::move(status.error()));
        }
        if (kind == DialogKind::SaveFile &&
            appendDefaultExtension(*selectedPath, defaultExtension)) {
            std::error_code error;
            const bool exists = std::filesystem::exists(
                std::filesystem::path{*selectedPath}, error);
            if (error) {
                return Core::failure(
                    Core::CoreErrorCode::Io,
                    "Linux editor save-file extension target could not be inspected");
            }
            if (exists) {
                return Core::failure(
                    Core::CoreErrorCode::AlreadyExists,
                    "Linux editor save-file extension target must be selected explicitly");
            }
        }
        if (auto status = validateSelectedPath(*selectedPath, kind); !status) {
            return Core::failure(std::move(status.error()));
        }
        return EditorFileDialogResult{
            .outcome = EditorFileDialogOutcome::Selected,
            .selectedPathUtf8 = std::move(*selectedPath),
        };
    }

    return Core::failure(Core::CoreErrorCode::Unsupported,
                         "Linux editor file dialogs require zenity or kdialog");
}

[[nodiscard]] Core::Status validateCommonRequest(
    std::string_view title,
    std::string_view initialDirectory,
    std::span<const EditorFileDialogFilter> filters)
{
    if (auto status = validateText(title, "title"); !status) {
        return status;
    }
    if (auto status = validateText(initialDirectory, "initial directory"); !status) {
        return status;
    }
    return validateFilters(filters);
}

[[nodiscard]] Core::Error allocationError()
{
    return Core::Error{Core::CoreErrorCode::OutOfMemory,
                       "Linux editor file dialog allocation failed"};
}

} // namespace

Core::Result<EditorFileDialogResult>
openExistingFileLinux(const OpenExistingFileDialogRequest& request)
{
    try {
        if (auto status = validateCommonRequest(request.titleUtf8,
                                                request.initialDirectoryUtf8,
                                                request.filters);
            !status) {
            return Core::failure(std::move(status.error()));
        }
        return showLinuxDialog(DialogKind::OpenFile, request.titleUtf8,
                               request.initialDirectoryUtf8, {}, {}, request.filters);
    } catch (const std::bad_alloc&) {
        return Core::failure(allocationError());
    }
}

Core::Result<EditorFileDialogResult>
saveFileLinux(const SaveFileDialogRequest& request)
{
    try {
        if (auto status = validateCommonRequest(request.titleUtf8,
                                                request.initialDirectoryUtf8,
                                                request.filters);
            !status) {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateText(request.suggestedFileNameUtf8,
                                       "suggested file name");
            !status) {
            return Core::failure(std::move(status.error()));
        }
        if (request.suggestedFileNameUtf8.contains('/') ||
            request.suggestedFileNameUtf8.contains('\\') ||
            request.suggestedFileNameUtf8 == "." ||
            request.suggestedFileNameUtf8 == "..") {
            return Core::failure(
                Core::CoreErrorCode::InvalidArgument,
                "Linux editor save-file suggested name must be a single file name");
        }
        if (auto status = validateText(request.defaultExtensionUtf8,
                                       "default extension");
            !status) {
            return Core::failure(std::move(status.error()));
        }
        if (request.defaultExtensionUtf8.starts_with('.') ||
            request.defaultExtensionUtf8.contains('/') ||
            request.defaultExtensionUtf8.contains('\\')) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Linux editor save-file default extension must not "
                                 "contain a path separator or start with a dot");
        }
        return showLinuxDialog(DialogKind::SaveFile, request.titleUtf8,
                               request.initialDirectoryUtf8,
                               request.suggestedFileNameUtf8,
                               request.defaultExtensionUtf8,
                               request.filters);
    } catch (const std::bad_alloc&) {
        return Core::failure(allocationError());
    }
}

Core::Result<EditorFileDialogResult>
pickFolderLinux(const PickFolderDialogRequest& request)
{
    try {
        if (auto status = validateCommonRequest(request.titleUtf8,
                                                request.initialDirectoryUtf8, {});
            !status) {
            return Core::failure(std::move(status.error()));
        }
        return showLinuxDialog(DialogKind::PickFolder, request.titleUtf8,
                               request.initialDirectoryUtf8, {}, {}, {});
    } catch (const std::bad_alloc&) {
        return Core::failure(allocationError());
    }
}

} // namespace Tina::EditorApp::Detail
