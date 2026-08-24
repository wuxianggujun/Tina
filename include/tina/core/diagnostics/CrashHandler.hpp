#pragma once

#include <tina/core/base/Compiler.hpp>

#include <string_view>

namespace Tina::Core::Diagnostics {

// Last-resort reporting for the failure modes a try/catch at main() cannot see:
// std::terminate (including every std::terminate() call inside the engine), a
// std::abort, and — on Windows — structured exceptions such as access
// violations and stack overflow. Without this the process disappears silently
// and leaves no way to tell a crash apart from a clean exit.
//
// The report is written with OS primitives only (no allocation, no iostreams,
// no Diagnostics instance) because the heap may already be corrupt and a
// stack-overflow report runs on a guard page with almost no room left.
//
// Install once, as early in main()/wWinMain() as possible. Not thread-safe with
// respect to itself; concurrent crashes are serialised and only the first is
// reported in full.
struct CrashHandlerConfig final {
    // Written into the report so multi-executable logs stay attributable.
    std::string_view applicationName{};
    // Absolute or relative path of a crash report file. Empty writes to stderr
    // only. The file is opened at crash time, not install time, so a stale
    // handle cannot keep the process alive.
    std::string_view reportPathUtf8{};
    // Emit a resolved symbol backtrace. Requires debug info to be present; the
    // report still names the reason when symbols are unavailable.
    bool captureBacktrace = true;
};

// Returns false when the platform refused to install a handler, in which case
// the process keeps its previous behaviour. Safe to call more than once; later
// calls replace the configuration.
TINA_CORE_API bool installCrashHandler(const CrashHandlerConfig& config) noexcept;

// Restores the handlers captured at install time. Mainly for tests that must
// exercise a real crash without taking the whole test binary down.
TINA_CORE_API void uninstallCrashHandler() noexcept;

// Number of crash reports written since install. A test can assert a report was
// produced without parsing the output.
[[nodiscard]] TINA_CORE_API unsigned long long crashReportCount() noexcept;

// Writes a report for a caller-detected fatal condition and terminates, taking
// the same path a crash would. Use where the engine currently calls
// std::terminate() directly so the reason survives.
[[noreturn]] TINA_CORE_API void reportFatalAndTerminate(std::string_view reason) noexcept;

} // namespace Tina::Core::Diagnostics
