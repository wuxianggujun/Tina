#include <tina/editor_app/EditorApplication.hpp>
#include <tina/core/base/Types.hpp>

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
    const Tina::Core::usize wideLength = std::char_traits<wchar_t>::length(wide);
    if (wideLength > static_cast<Tina::Core::usize>((std::numeric_limits<int>::max)())) {
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
    utf8.resize(static_cast<Tina::Core::usize>(utf8Length));
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
        std::vector<std::string> utf8Arguments(static_cast<Tina::Core::usize>(argumentCount));
        for (int index = 0; index < argumentCount; ++index) {
            if (!utf8FromWide(wideArguments.get()[index],
                              utf8Arguments[static_cast<Tina::Core::usize>(index)])) {
                return 2;
            }
        }

        std::vector<char*> argumentPointers;
        argumentPointers.reserve(utf8Arguments.size());
        for (std::string& argument : utf8Arguments) {
            argumentPointers.push_back(argument.data());
        }
        const int exitCode = Tina::EditorApp::runEditorApplication(
            static_cast<int>(argumentPointers.size()), argumentPointers.data());
        // A double-click has no console. Keep parameterized runs non-modal so
        // finite-frame automation can always observe the exit code and log.
        if (exitCode != 0 && argumentCount == 1) {
            ::MessageBoxW(nullptr,
                          L"Tina Editor 因错误退出。\n\n"
                          L"请在文件资源管理器地址栏中打开以下诊断日志：\n"
                          L"%TEMP%\\tina_editor_crash.txt",
                          L"Tina Editor 启动或运行失败", MB_OK | MB_ICONERROR);
        }
        return exitCode;
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
