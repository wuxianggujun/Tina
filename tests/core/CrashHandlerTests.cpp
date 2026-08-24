#include <gtest/gtest.h>

#include <tina/core/diagnostics/CrashHandler.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace Tina::Tests {
namespace {

using namespace Core::Diagnostics;

[[nodiscard]] std::filesystem::path reportPath(const char* name)
{
    std::error_code error;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
    return (error ? std::filesystem::path{"."} : temp) / name;
}

[[nodiscard]] std::string readAll(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return {};
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

// The engine reports fatal invariants through this entry point, so it is the one
// path a test can exercise without provoking real undefined behaviour.
TEST(CrashHandlerDeathTest, FatalReportNamesTheReasonAndExitsNonZero)
{
    EXPECT_DEATH(
        {
            (void)installCrashHandler(CrashHandlerConfig{
                .applicationName = "tina_crash_probe",
                .reportPathUtf8 = {},
                // Symbol resolution is slow and irrelevant to the contract here.
                .captureBacktrace = false,
            });
            reportFatalAndTerminate("preview registry still owned a live binding");
        },
        "preview registry still owned a live binding");
}

// std::terminate is how most engine invariants fail. Before the handler existed
// this produced no output at all, which is what made such crashes unreadable.
TEST(CrashHandlerDeathTest, TerminateIsReportedRatherThanSilent)
{
    EXPECT_DEATH(
        {
            (void)installCrashHandler(CrashHandlerConfig{
                .applicationName = "tina_crash_probe",
                .reportPathUtf8 = {},
                .captureBacktrace = false,
            });
            std::terminate();
        },
        "std::terminate was called");
}

// An exception that reaches std::terminate while still in flight must have its
// message pulled into the report; the type alone does not say what went wrong.
// The throw is wrapped in a noexcept call because gtest's death test would
// otherwise catch the exception itself and never let it reach terminate.
void throwPastNoexcept() noexcept
{
    throw std::runtime_error{"catalog reload lost its snapshot"};
}

TEST(CrashHandlerDeathTest, InFlightExceptionMessageReachesTheReport)
{
    EXPECT_DEATH(
        {
            (void)installCrashHandler(CrashHandlerConfig{
                .applicationName = "tina_crash_probe",
                .reportPathUtf8 = {},
                .captureBacktrace = false,
            });
            throwPastNoexcept();
        },
        "catalog reload lost its snapshot");
}

// Gates scrape stdout/stderr for a JSON status object. A crash has to be
// distinguishable from a clean exit, not just an empty stream.
TEST(CrashHandlerDeathTest, ReportCarriesMachineReadableCrashStatus)
{
    EXPECT_DEATH(
        {
            (void)installCrashHandler(CrashHandlerConfig{
                .applicationName = "tina_crash_probe",
                .reportPathUtf8 = {},
                .captureBacktrace = false,
            });
            reportFatalAndTerminate("scripted failure");
        },
        "\"status\":\"crash\"");
}

TEST(CrashHandlerTest, ReportFileReceivesTheCrashTextForGuiProcesses)
{
    // A GUI-subsystem binary has no visible stderr, so the file is the only
    // artifact the user can read. Verify it is actually written.
    const std::filesystem::path path = reportPath("tina_crash_handler_test.txt");
    std::error_code removeError;
    std::filesystem::remove(path, removeError);

    const std::string pathUtf8 = path.string();
    ASSERT_DEATH(
        {
            (void)installCrashHandler(CrashHandlerConfig{
                .applicationName = "tina_crash_probe",
                .reportPathUtf8 = pathUtf8,
                .captureBacktrace = false,
            });
            reportFatalAndTerminate("written to the report file");
        },
        "written to the report file");

    const std::string contents = readAll(path);
    EXPECT_NE(contents.find("written to the report file"), std::string::npos)
        << "report file contents: " << contents;
    EXPECT_NE(contents.find("tina_crash_probe"), std::string::npos);
    std::filesystem::remove(path, removeError);
}

// The backtrace is the whole point of the handler: without it a report names a
// reason but not the code that produced it. Verify frames are actually resolved
// rather than trusting the flag.
TEST(CrashHandlerTest, BacktraceNamesTheCallingFunction)
{
    const std::filesystem::path path = reportPath("tina_crash_backtrace_test.txt");
    std::error_code removeError;
    std::filesystem::remove(path, removeError);

    const std::string pathUtf8 = path.string();
    ASSERT_DEATH(
        {
            (void)installCrashHandler(CrashHandlerConfig{
                .applicationName = "tina_crash_probe",
                .reportPathUtf8 = pathUtf8,
                .captureBacktrace = true,
            });
            reportFatalAndTerminate("backtrace probe");
        },
        "backtrace");

    const std::string contents = readAll(path);
    EXPECT_NE(contents.find("backtrace:"), std::string::npos)
        << "report file contents: " << contents;
    // reportFatalAndTerminate is on the captured stack, so its symbol proves the
    // frames were resolved and not just printed as bare addresses.
    EXPECT_NE(contents.find("reportFatalAndTerminate"), std::string::npos)
        << "backtrace did not resolve symbols; contents: " << contents;
    std::filesystem::remove(path, removeError);
}

TEST(CrashHandlerTest, InstallIsIdempotentAndUninstallRestoresPreviousHandlers)
{
    const std::terminate_handler before = std::get_terminate();
    EXPECT_TRUE(installCrashHandler(CrashHandlerConfig{
        .applicationName = "tina_crash_probe",
        .captureBacktrace = false,
    }));
    EXPECT_NE(std::get_terminate(), before);
    // A second install only replaces configuration; it must not chain handlers
    // onto themselves and recurse at crash time.
    EXPECT_TRUE(installCrashHandler(CrashHandlerConfig{
        .applicationName = "tina_crash_probe_renamed",
        .captureBacktrace = false,
    }));

    uninstallCrashHandler();
    EXPECT_EQ(std::get_terminate(), before);
    // Uninstalling twice is harmless.
    uninstallCrashHandler();
    EXPECT_EQ(std::get_terminate(), before);
}

} // namespace
} // namespace Tina::Tests
