#include <tina/desktop/UiFontFile.hpp>

#include "core/io/PathUtil.hpp"

#include <tina/core/io/ApplicationPaths.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/Utf8.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory_resource>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Tina::Desktop {
namespace {

[[nodiscard]] Core::Result<std::string> environmentValue(const char* name)
{
#if defined(_WIN32)
    const std::string_view asciiName{name};
    const std::wstring wideName(asciiName.begin(), asciiName.end());
    std::wstring wideValue(32768, L'\0');
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableW(wideName.c_str(), wideValue.data(), static_cast<DWORD>(wideValue.size()));
    if (length == 0)
    {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND || GetLastError() == ERROR_SUCCESS) { return std::string{}; }
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "Unable to read UI font environment variable");
    }
    if (length >= wideValue.size())
    { return Core::failure(Core::CoreErrorCode::CapacityExceeded, "UI font environment variable is too long"); }
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideValue.data(), static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
    { return Core::failure(Core::CoreErrorCode::InvalidArgument, "UI font environment variable contains invalid UTF-16"); }
    std::string value(static_cast<usize>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideValue.data(), static_cast<int>(length), value.data(), bytes, nullptr, nullptr) != bytes)
    { return Core::failure(Core::CoreErrorCode::InvalidArgument, "Unable to encode UI font path as UTF-8"); }
    return value;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return std::string{};
    }
    if (!Core::isStrictUtf8WithoutNul(value))
    { return Core::failure(Core::CoreErrorCode::InvalidArgument, "UI font environment variable must be UTF-8"); }
    return std::string{value};
#endif
}

using Core::Detail::pathFromUtf8Bytes;
using Core::Detail::pathToUtf8;

Core::Result<std::shared_ptr<std::vector<std::byte>>> readFontBlob(std::string_view path, usize maxBytes)
{
    auto bytes = Core::readFile(path, {.maxBytes = maxBytes, .memoryResource = std::pmr::get_default_resource()});
    if (!bytes) { return Core::failure(bytes.error()); }
    return std::make_shared<std::vector<std::byte>>(bytes->begin(), bytes->end());
}

// Existence is checked before reading so a missing font stays a null result while a
// present but unreadable one surfaces the read error.
[[nodiscard]] bool isExistingFile(std::string_view utf8Path)
{
    std::error_code errorCode;
    return std::filesystem::is_regular_file(pathFromUtf8Bytes(utf8Path), errorCode);
}

[[nodiscard]] Core::Result<UiFontFile> readFontFile(std::string path, UiFontSource source)
{
    // readFile rejects a null memoryResource, so the default resource is named here
    // rather than left defaulted.
    auto bytes = Core::readFile(path, Core::ReadFileConfig{
                                          .maxBytes = Core::MaxReadFileBytes,
                                          .memoryResource = std::pmr::get_default_resource(),
                                      });
    if (!bytes)
    {
        return Core::failure(std::move(bytes.error()));
    }
    UiFontFile font{};
    font.bytes = std::make_shared<std::vector<std::byte>>(bytes->begin(), bytes->end());
    font.path = std::move(path);
    font.source = source;
    auto seedPath = pathFromUtf8Bytes(font.path);
    seedPath.replace_extension(".tmsdf");
    if (isExistingFile(pathToUtf8(seedPath)))
    {
        auto seed = readFontBlob(pathToUtf8(seedPath), 80U * 1024U * 1024U);
        if (!seed) { return Core::failure(seed.error()); }
        font.atlasBytes = std::move(*seed);
    }
    const auto appendFallback = [&](std::string_view path) -> Core::Status {
        if (font.fallbackBytes.size() >= 7)
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded, "UI font chain exceeds seven fallback faces");
        }
        auto bytes = readFontBlob(path, 64U * 1024U * 1024U);
        if (!bytes) { return Core::failure(bytes.error()); }
        font.fallbackBytes.push_back(std::move(*bytes));
        return Core::success();
    };
    auto fallbackEnvironment = environmentValue("TINA_UI_FALLBACK_FONT_PATHS");
    if (!fallbackEnvironment) { return Core::failure(fallbackEnvironment.error()); }
    const std::string_view environmentFallback = *fallbackEnvironment;
    if (!environmentFallback.empty())
    {
        usize begin = 0;
        while (begin < environmentFallback.size())
        {
            const usize separator = environmentFallback.find(';', begin);
            const usize end = separator == std::string_view::npos ? environmentFallback.size() : separator;
            if (end == begin) { return Core::failure(Core::CoreErrorCode::InvalidArgument, "Empty UI fallback font path"); }
            if (auto status = appendFallback(environmentFallback.substr(begin, end - begin)); !status)
            { return Core::failure(status.error()); }
            begin = end + 1U;
        }
    }
    else
    {
        const auto directory = pathFromUtf8Bytes(font.path).parent_path();
        const std::string manifestPath = pathToUtf8(directory / "ui-font-fallbacks.txt");
        if (isExistingFile(manifestPath))
        {
            auto bytes = readFontBlob(manifestPath, 4096);
            if (!bytes) { return Core::failure(bytes.error()); }
            const std::string_view manifest(reinterpret_cast<const char*>((*bytes)->data()), (*bytes)->size());
            if (!Core::isStrictUtf8WithoutNul(manifest))
            { return Core::failure(Core::CoreErrorCode::InvalidArgument, "Font fallback manifest is not strict UTF-8"); }
            usize begin = 0;
            while (begin < manifest.size())
            {
                const usize newline = manifest.find('\n', begin);
                const usize end = newline == std::string_view::npos ? manifest.size() : newline;
                std::string_view name = manifest.substr(begin, end - begin);
                if (!name.empty() && name.back() == '\r') { name.remove_suffix(1); }
                if (!name.empty())
                {
                    if (name == "." || name == ".." || name.find_first_of("/\\:") != std::string_view::npos)
                    { return Core::failure(Core::CoreErrorCode::InvalidArgument, "Fallback manifest entries must be plain file names"); }
                    if (auto status = appendFallback(pathToUtf8(directory / pathFromUtf8Bytes(name))); !status)
                    { return Core::failure(status.error()); }
                }
                begin = end + 1U;
            }
        }
    }
    return font;
}

} // namespace

Core::Result<UiFontFile> resolveUiFontBytes(const char* relativePath)
try
{
    auto environment = environmentValue("TINA_UI_FONT_PATH");
    if (!environment) { return Core::failure(environment.error()); }
    if (const std::string_view fromEnvironment = *environment; !fromEnvironment.empty())
    {
        if (isExistingFile(fromEnvironment))
        {
            return readFontFile(std::string{fromEnvironment}, UiFontSource::Environment);
        }
        return Core::failure(Core::CoreErrorCode::NotFound,
                             "TINA_UI_FONT_PATH does not name an existing file");
    }

    if (relativePath == nullptr || *relativePath == '\0')
    {
        return UiFontFile{};
    }
    auto beside = Core::applicationFilePath(relativePath);
    if (!beside)
    {
        return Core::failure(std::move(beside.error()));
    }
    if (!isExistingFile(*beside))
    {
        return UiFontFile{};
    }
    return readFontFile(std::move(*beside), UiFontSource::BesideExecutable);
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "UI font resolution allocation failed");
}
catch (const std::system_error&)
{
    return Core::failure(Core::CoreErrorCode::InvalidArgument, "UI font path is not a valid filesystem path");
}

} // namespace Tina::Desktop
