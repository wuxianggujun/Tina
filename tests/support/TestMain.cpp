#include <gtest/gtest.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#if defined(_MSC_VER)
#include <crtdbg.h>
#include <cstdlib>
#endif
#endif

namespace {
void configureNoninteractiveDiagnostics() noexcept
{
#if defined(_WIN32)
    // Only this process and its children: no OS security/approval settings.
    constexpr UINT flags = SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
    const UINT previous = SetErrorMode(flags);
    SetErrorMode(previous | flags);
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(_WRITE_ABORT_MSG, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
#endif
}
} // namespace

int main(int argc, char** argv)
{
    configureNoninteractiveDiagnostics();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
