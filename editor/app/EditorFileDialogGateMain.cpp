#include "EditorFileDialog.hpp"

#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

enum class Operation : std::uint8_t {
    Open,
    Save,
    Folder,
};

struct Options final {
    std::optional<Operation> operation{};
    std::string operationName{};
    std::string initialDirectory{};
    std::string suggestedName{};
    std::string defaultExtension{};
    std::string titleToken{};
};

[[nodiscard]] bool assignOnce(std::string& destination, std::string_view value, std::string_view optionName)
{
    if (!destination.empty())
    {
        std::cerr << optionName << " may only be specified once\n";
        return false;
    }
    destination.assign(value);
    return true;
}

[[nodiscard]] bool parseOperation(Options& options, std::string_view value)
{
    if (options.operation.has_value())
    {
        std::cerr << "--operation may only be specified once\n";
        return false;
    }
    if (value == "open")
    {
        options.operation = Operation::Open;
    } else if (value == "save")
    {
        options.operation = Operation::Save;
    } else if (value == "folder")
    {
        options.operation = Operation::Folder;
    } else
    {
        std::cerr << "--operation must be open, save, or folder\n";
        return false;
    }
    options.operationName.assign(value);
    return true;
}

[[nodiscard]] bool parseOptions(int argumentCount, char** arguments, Options& options)
{
    Tina::Core::ArgScanner scanner(argumentCount, arguments);
    while (scanner.next())
    {
        if (const auto value = scanner.value("--operation"))
        {
            if (!parseOperation(options, *value))
            {
                return false;
            }
        } else if (const auto value = scanner.value("--initial-directory"))
        {
            if (!assignOnce(options.initialDirectory, *value, "--initial-directory"))
            {
                return false;
            }
        } else if (const auto value = scanner.value("--suggested-name"))
        {
            if (!assignOnce(options.suggestedName, *value, "--suggested-name"))
            {
                return false;
            }
        } else if (const auto value = scanner.value("--default-extension"))
        {
            if (!assignOnce(options.defaultExtension, *value, "--default-extension"))
            {
                return false;
            }
        } else if (const auto value = scanner.value("--title-token"))
        {
            if (!assignOnce(options.titleToken, *value, "--title-token"))
            {
                return false;
            }
        } else if (scanner.failed())
        {
            std::cerr << "missing value for " << scanner.failedOption() << '\n';
            return false;
        } else
        {
            std::cerr << "unknown option: " << scanner.token() << '\n';
            return false;
        }
    }

    if (!options.operation.has_value() || options.initialDirectory.empty() || options.titleToken.empty())
    {
        std::cerr << "--operation, --initial-directory, and --title-token are required\n";
        return false;
    }
    if (!std::filesystem::path{options.initialDirectory}.is_absolute())
    {
        std::cerr << "--initial-directory must be absolute\n";
        return false;
    }
    if (*options.operation == Operation::Save && (options.suggestedName.empty() || options.defaultExtension.empty()))
    {
        std::cerr << "save requires --suggested-name and --default-extension\n";
        return false;
    }
    if (*options.operation != Operation::Save && (!options.suggestedName.empty() || !options.defaultExtension.empty()))
    {
        std::cerr << "save-only options were provided for a non-save operation\n";
        return false;
    }
    return true;
}

[[nodiscard]] Tina::Core::Result<Tina::EditorApp::Detail::EditorFileDialogResult> runDialog(const Options& options)
{
    using namespace Tina::EditorApp::Detail;

    const std::string title = "TinaEditorDialogGate-" + options.titleToken;
    const std::array filters{
        EditorFileDialogFilter{"Tina World 2D", "*.tworld"},
    };
    const EditorFileDialog dialog{};

    switch (*options.operation)
    {
    case Operation::Open:
        return dialog.openExistingFile(OpenExistingFileDialogRequest{
            .titleUtf8 = title,
            .initialDirectoryUtf8 = options.initialDirectory,
            .filters = filters,
        });
    case Operation::Save:
        return dialog.saveFile(SaveFileDialogRequest{
            .titleUtf8 = title,
            .initialDirectoryUtf8 = options.initialDirectory,
            .suggestedFileNameUtf8 = options.suggestedName,
            .defaultExtensionUtf8 = options.defaultExtension,
            .filters = filters,
        });
    case Operation::Folder:
        return dialog.pickFolder(PickFolderDialogRequest{
            .titleUtf8 = title,
            .initialDirectoryUtf8 = options.initialDirectory,
        });
    }

    return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "unreachable file dialog operation");
}

void writeError(const Tina::Core::Error& error)
{
    {
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("schema", 1);
        writer.member("status", "error");
        writer.member("domain", static_cast<std::uint16_t>(error.code.domain));
        writer.member("code", error.code.value);
        if (error.nativeCode.has_value())
        {
            writer.member("nativeCode", *error.nativeCode);
        } else
        {
            writer.rawMember("nativeCode", "null");
        }
        writer.member("message", error.message);
        writer.endObject();
    }
    std::cerr << '\n';
}

} // namespace

int main(int argumentCount, char** arguments)
{
    Options options{};
    if (!parseOptions(argumentCount, arguments, options))
    {
        return 2;
    }

    auto result = runDialog(options);
    if (!result)
    {
        writeError(result.error());
        return 3;
    }

    {
        Tina::Core::JsonWriter writer(std::cout);
        writer.beginObject();
        writer.member("schema", 1);
        writer.member("operation", options.operationName);
        writer.member("outcome", result->selected() ? "selected" : "cancelled");
        writer.member("path", result->selectedPathUtf8);
        writer.endObject();
    }
    std::cout << '\n';
    return 0;
}
