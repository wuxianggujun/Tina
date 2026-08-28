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

// The report path is UTF-8 by contract. Handing it to the ANSI Win32 API turned
// any non-ASCII path into a mojibake filename or an outright failure -- and it
// failed on exactly the machines whose user profile is not ASCII, which is the
// worst possible place for a diagnostic to go missing.
TEST(CrashHandlerTest, OpensAReportFileWhoseUtf8PathIsNotAscii)
{
    std::error_code error;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
    const std::filesystem::path base = error ? std::filesystem::path{"."} : temp;
    // Han characters plus an accented Latin letter: both are multi-byte in UTF-8
    // and neither survives a codepage reinterpretation intact.
    const std::filesystem::path directory = base / L"tina_崩溃_réports";
    std::filesystem::remove_all(directory, error);
    ASSERT_TRUE(std::filesystem::create_directories(directory, error)) << error.message();
    const std::filesystem::path path = directory / L"报告_cräsh.txt";

    // The UTF-8 bytes are what the API contract specifies, so that is what is
    // passed -- not the platform-native wide string.
    const std::string utf8Path = path.u8string().empty()
                                     ? std::string{}
                                     : std::string{reinterpret_cast<const char*>(
                                           path.u8string().c_str())};
    ASSERT_FALSE(utf8Path.empty());

    EXPECT_TRUE(installCrashHandler(CrashHandlerConfig{
        .applicationName = "tina_crash_utf8",
        .reportPathUtf8 = utf8Path,
        .captureBacktrace = false,
    }));
    uninstallCrashHandler();

    // The armed marker proves the handler opened *this* file rather than a
    // codepage-mangled sibling.
    EXPECT_TRUE(std::filesystem::exists(path, error))
        << "no report at the requested UTF-8 path; a mangled name means the ANSI "
           "API was used";
    std::filesystem::remove_all(directory, error);
}

// Install returns whether the requested report file is usable, because a crash
// that can only reach stderr is lost by any gate that does not capture stderr.
// Returning true unconditionally hid that.
TEST(CrashHandlerTest, InstallReportsWhetherTheRequestedReportFileIsUsable)
{
    // No file requested: stderr is always available, so this is ready.
    EXPECT_TRUE(installCrashHandler(CrashHandlerConfig{
        .applicationName = "tina_crash_stderr_only",
        .captureBacktrace = false,
    }));
    uninstallCrashHandler();

    // A path inside a directory that does not exist cannot be opened, and the
    // caller has to be able to see that before the crash rather than after.
    std::error_code error;
    const std::filesystem::path missing =
        reportPath("tina_crash_absent_dir") / "nested" / "report.txt";
    std::filesystem::remove_all(reportPath("tina_crash_absent_dir"), error);
    EXPECT_FALSE(installCrashHandler(CrashHandlerConfig{
        .applicationName = "tina_crash_unopenable",
        .reportPathUtf8 = missing.string(),
        .captureBacktrace = false,
    }));
    uninstallCrashHandler();
}

// A path longer than the internal fixed buffer used to be truncated silently. If
// the cut landed on a valid boundary, CREATE_ALWAYS created the shortened name and
// install returned true, so every crash went to a file nobody would look for.
//
// Note this specific input is also past Windows MAX_PATH, so it would be refused
// even without the length check -- what this pins is the *contract* (an
// over-long path is never reported as ready), not the truncation mechanism on its
// own. The mechanism is covered by copyBounded returning whether the source fit,
// which install now requires before opening anything.
TEST(CrashHandlerTest, RefusesAReportPathTooLongForItsFixedBuffer)
{
    std::error_code error;
    const std::filesystem::path directory = reportPath("tina_crash_long_path");
    std::filesystem::remove_all(directory, error);
    ASSERT_TRUE(std::filesystem::create_directories(directory, error)) << error.message();

    std::string longName(700, 'x');
    longName += ".txt";
    const std::filesystem::path path = directory / longName;

    EXPECT_FALSE(installCrashHandler(CrashHandlerConfig{
        .applicationName = "tina_crash_long",
        .reportPathUtf8 = path.string(),
        .captureBacktrace = false,
    }));
    uninstallCrashHandler();

    // Nothing was created under a shortened name.
    std::size_t created = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        static_cast<void>(entry);
        ++created;
    }
    EXPECT_EQ(created, 0U) << "a truncated path was opened instead of being refused";
    std::filesystem::remove_all(directory, error);
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
