#pragma once

#include <tina/core/base/Compiler.hpp>

#include <string_view>

namespace Tina::Core::Diagnostics {

// Best-effort, last-resort reporting for failure modes a try/catch at main()
// cannot see:
// std::terminate (including every std::terminate() call inside the engine), a
// std::abort, and — on Windows — structured exceptions such as access
// violations and stack overflow. Without this the process disappears silently
// and leaves no way to tell a crash apart from a clean exit.
//
// The report uses fixed storage and avoids dynamic allocation, C++ iostreams,
// and the Diagnostics owner. Symbol resolution and platform/CRT writes are
// still best effort because the process may already be corrupted.
//
// Install once, as early in main()/wWinMain() as possible. Installation and
// removal must not race each other or a crash. Concurrent crashes are
// serialised and only the first is reported in full.
struct CrashHandlerConfig final {
    // Written into the report so multi-executable logs stay attributable.
    std::string_view applicationName{};
    // Absolute or relative UTF-8 path of a crash report file. Empty writes to
    // stderr only. Windows pre-opens and truncates the file during installation
    // so a damaged process does not need to open it; other platforms open it
    // while reporting.
    std::string_view reportPathUtf8{};
    // Emit a resolved symbol backtrace. Requires debug info to be present; the
    // report still names the reason when symbols are unavailable.
    bool captureBacktrace = true;
};

// Safe to call more than once; later calls replace the configuration.
//
// Returns whether the requested report file is usable: true when reportPathUtf8
// is empty (stderr only), and otherwise only when the file was actually opened.
// A false return means a crash will reach stderr and nothing else, which a gate
// that does not capture stderr would lose entirely — so it is worth acting on.
//
// The hooks themselves stay best effort and do not affect the result: a partially
// hooked process still reports more than an unhooked one, and a caller cannot do
// anything useful about a refused platform filter.
[[nodiscard]] TINA_CORE_API bool installCrashHandler(const CrashHandlerConfig& config) noexcept;

// Removes Tina-owned hooks and restores the captured std::terminate handler.
// Mainly for isolated tests; it is not a general signal/filter chaining API.
TINA_CORE_API void uninstallCrashHandler() noexcept;

// Process-lifetime number of crash reports written. A test can assert a report
// was produced without parsing the output.
[[nodiscard]] TINA_CORE_API unsigned long long crashReportCount() noexcept;

// Writes a report for a caller-detected fatal condition and terminates, taking
// the same path a crash would. Use where the engine currently calls
// std::terminate() directly so the reason survives.
[[noreturn]] TINA_CORE_API void reportFatalAndTerminate(std::string_view reason) noexcept;

} // namespace Tina::Core::Diagnostics
