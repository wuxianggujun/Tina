#include <tina/desktop/UiFontFile.hpp>

#include "core/io/PathUtil.hpp"

#include <tina/core/io/ApplicationPaths.hpp>
#include <tina/core/io/ReadFile.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory_resource>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Tina::Desktop {
namespace {

[[nodiscard]] std::string_view environmentValue(const char* name) noexcept
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return {};
    }
    return std::string_view{value};
}

using Core::Detail::pathFromUtf8Bytes;

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
    return font;
}

} // namespace

Core::Result<UiFontFile> resolveUiFontBytes(const char* relativePath)
try
{
    if (const std::string_view fromEnvironment = environmentValue("TINA_UI_FONT_PATH");
        !fromEnvironment.empty())
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

} // namespace Tina::Desktop
