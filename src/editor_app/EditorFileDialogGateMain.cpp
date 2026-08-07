#include "EditorFileDialog.hpp"

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

void writeJsonString(std::ostream& output, std::string_view value)
{
    constexpr char Hexadecimal[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char byte : value)
    {
        switch (byte)
        {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (byte < 0x20U)
            {
                output << "\\u00" << Hexadecimal[byte >> 4U] << Hexadecimal[byte & 0x0FU];
            } else
            {
                output.put(static_cast<char>(byte));
            }
            break;
        }
    }
    output.put('"');
}

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
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
        constexpr std::string_view OperationPrefix = "--operation=";
        constexpr std::string_view InitialDirectoryPrefix = "--initial-directory=";
        constexpr std::string_view SuggestedNamePrefix = "--suggested-name=";
        constexpr std::string_view DefaultExtensionPrefix = "--default-extension=";
        constexpr std::string_view TitleTokenPrefix = "--title-token=";

        if (argument.starts_with(OperationPrefix))
        {
            if (!parseOperation(options, argument.substr(OperationPrefix.size())))
            {
                return false;
            }
        } else if (argument.starts_with(InitialDirectoryPrefix))
        {
            if (!assignOnce(options.initialDirectory, argument.substr(InitialDirectoryPrefix.size()),
                            "--initial-directory"))
            {
                return false;
            }
        } else if (argument.starts_with(SuggestedNamePrefix))
        {
            if (!assignOnce(options.suggestedName, argument.substr(SuggestedNamePrefix.size()), "--suggested-name"))
            {
                return false;
            }
        } else if (argument.starts_with(DefaultExtensionPrefix))
        {
            if (!assignOnce(options.defaultExtension, argument.substr(DefaultExtensionPrefix.size()),
                            "--default-extension"))
            {
                return false;
            }
        } else if (argument.starts_with(TitleTokenPrefix))
        {
            if (!assignOnce(options.titleToken, argument.substr(TitleTokenPrefix.size()), "--title-token"))
            {
                return false;
            }
        } else
        {
            std::cerr << "unknown option: " << argument << '\n';
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
    std::cerr << "{\"schema\":1,\"status\":\"error\",\"domain\":" << static_cast<std::uint16_t>(error.code.domain)
              << ",\"code\":" << error.code.value << ",\"nativeCode\":";
    if (error.nativeCode.has_value())
    {
        std::cerr << *error.nativeCode;
    } else
    {
        std::cerr << "null";
    }
    std::cerr << ",\"message\":";
    writeJsonString(std::cerr, error.message);
    std::cerr << "}\n";
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

    std::cout << "{\"schema\":1,\"operation\":";
    writeJsonString(std::cout, options.operationName);
    std::cout << ",\"outcome\":\"" << (result->selected() ? "selected" : "cancelled") << "\",\"path\":";
    writeJsonString(std::cout, result->selectedPathUtf8);
    std::cout << "}\n";
    return 0;
}
