#include "EditorSourceFileRename.hpp"

#include "core/io/PathUtil.hpp"

#include <tina/core/text/Utf8.hpp>

#include <filesystem>
#include <new>
#include <system_error>
#include <utility>

namespace Tina::EditorApp::Detail {
namespace {

using Core::Detail::pathFromUtf8Bytes;

[[nodiscard]] Core::Error filesystemError(std::string_view message,
                                          const std::error_code& error,
                                          std::string_view previousPath,
                                          std::string_view renamedPath)
{
    Core::Error failure{Core::CoreErrorCode::Io, message};
    failure.setNativeCode(error.value());
    failure.addContext("previousPath", previousPath);
    failure.addContext("renamedPath", renamedPath);
    return failure;
}

} // namespace

EditorSourceFileRename::EditorSourceFileRename(
    std::string previousPathUtf8, std::string renamedPathUtf8) noexcept
    : previousPathUtf8_(std::move(previousPathUtf8)),
      renamedPathUtf8_(std::move(renamedPathUtf8)), active_(true)
{
}

Core::Result<EditorSourceFileRename> EditorSourceFileRename::Begin(
    std::string_view previousPathUtf8,
    std::string_view renamedPathUtf8) noexcept
try
{
    if (previousPathUtf8.empty() || renamedPathUtf8.empty() ||
        previousPathUtf8 == renamedPathUtf8 ||
        !Core::isStrictUtf8WithoutNul(previousPathUtf8) ||
        !Core::isStrictUtf8WithoutNul(renamedPathUtf8))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source-file rename paths are invalid");
    }

    const auto previousPath = pathFromUtf8Bytes(previousPathUtf8);
    const auto renamedPath = pathFromUtf8Bytes(renamedPathUtf8);
    if (!previousPath.is_absolute() || !renamedPath.is_absolute())
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source-file rename paths must be absolute");
    }

    std::error_code error;
    const auto previousStatus = std::filesystem::symlink_status(previousPath, error);
    if (error || !std::filesystem::is_regular_file(previousStatus) ||
        std::filesystem::is_symlink(previousStatus))
    {
        if (error)
        {
            return Core::failure(filesystemError(
                "Editor could not inspect the source file before rename", error,
                previousPathUtf8, renamedPathUtf8));
        }
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source-file rename requires a physical regular file");
    }

    error.clear();
    const auto renamedStatus = std::filesystem::symlink_status(renamedPath, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return Core::failure(filesystemError(
            "Editor could not inspect the source-file rename destination", error,
            previousPathUtf8, renamedPathUtf8));
    }
    if (!error && std::filesystem::exists(renamedStatus))
    {
        std::error_code equivalentError;
        const bool sameFile = std::filesystem::equivalent(
            previousPath, renamedPath, equivalentError);
        if (equivalentError || !sameFile)
        {
            return Core::failure(Core::CoreErrorCode::AlreadyExists,
                                 "Editor source-file rename destination already exists");
        }
    }

    error.clear();
    std::filesystem::rename(previousPath, renamedPath, error);
    if (error)
    {
        return Core::failure(filesystemError(
            "Editor could not rename the source file", error,
            previousPathUtf8, renamedPathUtf8));
    }
    return EditorSourceFileRename{
        std::string(previousPathUtf8), std::string(renamedPathUtf8)};
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "Editor could not retain the source-file rename transaction");
}
catch (const std::filesystem::filesystem_error& exception)
{
    return Core::failure(Core::Error{
        Core::CoreErrorCode::Io,
        "Editor source-file rename filesystem operation failed"}
        .setNativeCode(exception.code().value()));
}

EditorSourceFileRename::~EditorSourceFileRename() noexcept
{
    rollbackNoexcept();
}

EditorSourceFileRename::EditorSourceFileRename(
    EditorSourceFileRename&& other) noexcept
    : previousPathUtf8_(std::move(other.previousPathUtf8_)),
      renamedPathUtf8_(std::move(other.renamedPathUtf8_)),
      active_(std::exchange(other.active_, false))
{
}

EditorSourceFileRename& EditorSourceFileRename::operator=(
    EditorSourceFileRename&& other) noexcept
{
    if (this != &other)
    {
        rollbackNoexcept();
        previousPathUtf8_ = std::move(other.previousPathUtf8_);
        renamedPathUtf8_ = std::move(other.renamedPathUtf8_);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

Core::Status EditorSourceFileRename::rollback() noexcept
try
{
    if (!active_)
    {
        return Core::success();
    }
    const auto previousPath = pathFromUtf8Bytes(previousPathUtf8_);
    const auto renamedPath = pathFromUtf8Bytes(renamedPathUtf8_);

    std::error_code error;
    const auto renamedStatus = std::filesystem::symlink_status(renamedPath, error);
    if (error || !std::filesystem::is_regular_file(renamedStatus) ||
        std::filesystem::is_symlink(renamedStatus))
    {
        if (error)
        {
            return Core::failure(filesystemError(
                "Editor could not inspect the renamed source file during rollback", error,
                previousPathUtf8_, renamedPathUtf8_));
        }
        return Core::failure(Core::CoreErrorCode::NotFound,
                             "Editor renamed source file disappeared before rollback");
    }

    error.clear();
    const auto previousStatus = std::filesystem::symlink_status(previousPath, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return Core::failure(filesystemError(
            "Editor could not inspect the original source path during rollback", error,
            previousPathUtf8_, renamedPathUtf8_));
    }
    if (!error && std::filesystem::exists(previousStatus))
    {
        std::error_code equivalentError;
        const bool sameFile = std::filesystem::equivalent(
            previousPath, renamedPath, equivalentError);
        if (equivalentError || !sameFile)
        {
            return Core::failure(Core::CoreErrorCode::AlreadyExists,
                                 "Editor source-file rollback destination is occupied");
        }
    }

    error.clear();
    std::filesystem::rename(renamedPath, previousPath, error);
    if (error)
    {
        return Core::failure(filesystemError(
            "Editor could not roll back the source-file rename", error,
            previousPathUtf8_, renamedPathUtf8_));
    }
    active_ = false;
    return Core::success();
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "Editor source-file rollback error reporting allocation failed");
}
catch (const std::filesystem::filesystem_error& exception)
{
    return Core::failure(Core::Error{
        Core::CoreErrorCode::Io,
        "Editor source-file rollback filesystem operation failed"}
        .setNativeCode(exception.code().value()));
}

void EditorSourceFileRename::rollbackNoexcept() noexcept
{
    if (!active_)
    {
        return;
    }
    try
    {
        const auto previousPath = pathFromUtf8Bytes(previousPathUtf8_);
        const auto renamedPath = pathFromUtf8Bytes(renamedPathUtf8_);
        std::error_code error;
        const auto previousStatus = std::filesystem::symlink_status(previousPath, error);
        if (!error && std::filesystem::exists(previousStatus))
        {
            return;
        }
        if (error != std::errc::no_such_file_or_directory)
        {
            return;
        }
        error.clear();
        std::filesystem::rename(renamedPath, previousPath, error);
        if (!error)
        {
            active_ = false;
        }
    }
    catch (...)
    {
    }
}

} // namespace Tina::EditorApp::Detail
