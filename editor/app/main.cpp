#include <tina/editor_app/EditorApplication.hpp>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

struct LocalFreeDeleter final {
    void operator()(wchar_t** value) const noexcept
    {
        if (value != nullptr) {
            ::LocalFree(value);
        }
    }
};

[[nodiscard]] bool utf8FromWide(const wchar_t* wide, std::string& utf8)
{
    if (wide == nullptr) {
        return false;
    }
    const std::size_t wideLength = std::char_traits<wchar_t>::length(wide);
    if (wideLength > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    if (wideLength == 0U) {
        utf8.clear();
        return true;
    }

    const int sourceLength = static_cast<int>(wideLength);
    const int utf8Length = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide, sourceLength, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) {
        return false;
    }
    utf8.resize(static_cast<std::size_t>(utf8Length));
    return ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, sourceLength,
                                 utf8.data(), utf8Length, nullptr, nullptr) == utf8Length;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argumentCount = 0;
    std::unique_ptr<wchar_t*, LocalFreeDeleter> wideArguments{
        ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount)};
    if (!wideArguments || argumentCount <= 0) {
        return 2;
    }

    try {
        std::vector<std::string> utf8Arguments(static_cast<std::size_t>(argumentCount));
        for (int index = 0; index < argumentCount; ++index) {
            if (!utf8FromWide(wideArguments.get()[index],
                              utf8Arguments[static_cast<std::size_t>(index)])) {
                return 2;
            }
        }

        std::vector<char*> argumentPointers;
        argumentPointers.reserve(utf8Arguments.size());
        for (std::string& argument : utf8Arguments) {
            argumentPointers.push_back(argument.data());
        }
        return Tina::EditorApp::runEditorApplication(
            static_cast<int>(argumentPointers.size()), argumentPointers.data());
    } catch (const std::bad_alloc&) {
        return 1;
    }
}

#else

int main(int argumentCount, char** arguments)
{
    return Tina::EditorApp::runEditorApplication(argumentCount, arguments);
}

#endif
