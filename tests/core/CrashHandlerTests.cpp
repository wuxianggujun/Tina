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
// reason but not the code that produced it.
//
// Symbol resolution is Windows-only today (DbgHelp); elsewhere `emitBacktrace`
// deliberately records that it is unavailable. Both branches assert something
// real rather than skipping: what must never happen is the section going missing
// silently, because then a reader cannot tell "no frames" from "no handler".
// This assertion was previously unguarded and failed on Linux for a defect that
// was never in the handler.
TEST(CrashHandlerTest, BacktraceIsResolvedWhereSupportedAndExplicitWhereNot)
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
        << "the report must always account for the backtrace, even when it has "
           "none; contents: "
        << contents;
#if defined(_WIN32)
    // reportFatalAndTerminate is on the captured stack, so its symbol proves the
    // frames were resolved and not just printed as bare addresses.
    EXPECT_NE(contents.find("reportFatalAndTerminate"), std::string::npos)
        << "backtrace did not resolve symbols; contents: " << contents;
#else
    // The absence has to be stated, not implied by an empty section.
    EXPECT_NE(contents.find("unavailable on this platform"), std::string::npos)
        << "a platform without symbol resolution must say so explicitly; contents: "
        << contents;
#endif
    std::filesystem::remove(path, removeError);
}

// Installation must leave an armed marker and must own the file for this run.
// Both are what docs/testing.md tells an operator to rely on when triaging a
// vanished window: a file with only a marker means the process died somewhere no
// handler can observe, while no file at all means the handler was never installed.
// This was Windows-only behaviour -- on other platforms the file did not exist
// until a crash, and then only grew, so a stale report from an earlier run could
// be mistaken for the current one.
TEST(CrashHandlerTest, InstallArmsTheReportFileAndTakesOwnershipOfThisRun)
{
    const std::filesystem::path path = reportPath("tina_crash_armed_marker_test.txt");
    std::error_code error;
    std::filesystem::remove(path, error);

    // A leftover report from a previous run must not survive into this one.
    {
        std::ofstream stale{path, std::ios::binary};
        ASSERT_TRUE(stale.is_open());
        stale << "stale report from an earlier run\n";
    }

    // The application name deliberately does not contain the word "armed": naming
    // it e.g. "tina_crash_armed" made the marker assertion below pass off the name
    // alone, so a mangled marker line still looked correct.
    EXPECT_TRUE(installCrashHandler(CrashHandlerConfig{
        .applicationName = "tina_crash_probe",
        .reportPathUtf8 = path.string(),
        .captureBacktrace = false,
    }));
    uninstallCrashHandler();

    const std::string contents = readAll(path);
    EXPECT_NE(contents.find("armed"), std::string::npos)
        << "installation left no armed marker; contents: " << contents;
    EXPECT_NE(contents.find("tina_crash_probe"), std::string::npos)
        << "the marker must name the application; contents: " << contents;
    EXPECT_EQ(contents.find("stale report from an earlier run"), std::string::npos)
        << "the previous run's report was not truncated; contents: " << contents;
    std::filesystem::remove(path, error);
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
