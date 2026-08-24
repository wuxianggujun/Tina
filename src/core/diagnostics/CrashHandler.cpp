#include <tina/core/diagnostics/CrashHandler.hpp>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
// DbgHelp must follow Windows.h.
#include <DbgHelp.h>
#include <crtdbg.h>
#include <cstdint>
#endif

namespace Tina::Core::Diagnostics {
namespace {

// Fixed storage: a crash report must not allocate. The name and path are copied
// at install time because the caller's string_view may already be dangling by
// the time we crash.
constexpr std::size_t MaxNameBytes = 64;
constexpr std::size_t MaxPathBytes = 512;
constexpr std::size_t MaxFrames = 62;

struct HandlerState final {
    char applicationName[MaxNameBytes]{};
    char reportPathUtf8[MaxPathBytes]{};
    bool captureBacktrace = true;
    bool installed = false;
};

HandlerState g_state{};
std::atomic<unsigned long long> g_reportCount{0};
// Serialises concurrent crashes so two threads cannot interleave one report.
std::atomic_flag g_reporting = ATOMIC_FLAG_INIT;
std::terminate_handler g_previousTerminate = nullptr;

void copyBounded(char* destination, std::size_t capacity, std::string_view source) noexcept
{
    const std::size_t count = source.size() < capacity - 1U ? source.size() : capacity - 1U;
    if (count != 0U)
    {
        std::memcpy(destination, source.data(), count);
    }
    destination[count] = '\0';
}

// Writes to stderr and, when configured, appends to the report file. Both use
// unbuffered primitives so a report survives an immediate process death.
void emit(const char* text) noexcept
{
    if (text == nullptr || text[0] == '\0')
    {
        return;
    }
    std::fputs(text, stderr);
    std::fflush(stderr);
    if (g_state.reportPathUtf8[0] != '\0')
    {
        if (std::FILE* file = std::fopen(g_state.reportPathUtf8, "ab"))
        {
            std::fputs(text, file);
            std::fflush(file);
            (void)std::fclose(file);
        }
    }
}

void emitLine(const char* prefix, const char* value) noexcept
{
    char line[1024]{};
    std::snprintf(line, sizeof(line), "%s%s\n", prefix, value != nullptr ? value : "");
    emit(line);
}

#if defined(_WIN32)
// Resolves the current call stack. Runs inside the crash path, so it only uses
// DbgHelp on a best-effort basis and never fails the report.
void emitBacktrace(CONTEXT* context) noexcept
{
    if (!g_state.captureBacktrace)
    {
        return;
    }
    const HANDLE process = ::GetCurrentProcess();
    // Symbols may be unavailable (stripped build, missing PDB); the frame
    // addresses are still worth reporting.
    const bool symbols = ::SymInitialize(process, nullptr, TRUE) != FALSE;

    void* frames[MaxFrames]{};
    USHORT captured = 0;
    if (context != nullptr)
    {
        // A structured exception hands us the faulting context, which is the
        // only way to see the frame that actually failed.
        STACKFRAME64 frame{};
        CONTEXT walkContext = *context;
#if defined(_M_X64)
        DWORD machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = walkContext.Rip;
        frame.AddrFrame.Offset = walkContext.Rbp;
        frame.AddrStack.Offset = walkContext.Rsp;
#elif defined(_M_ARM64)
        DWORD machine = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset = walkContext.Pc;
        frame.AddrFrame.Offset = walkContext.Fp;
        frame.AddrStack.Offset = walkContext.Sp;
#else
        DWORD machine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = walkContext.Eip;
        frame.AddrFrame.Offset = walkContext.Ebp;
        frame.AddrStack.Offset = walkContext.Esp;
#endif
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;
        while (captured < MaxFrames &&
               ::StackWalk64(machine, process, ::GetCurrentThread(), &frame, &walkContext,
                             nullptr, ::SymFunctionTableAccess64, ::SymGetModuleBase64,
                             nullptr) != FALSE)
        {
            if (frame.AddrPC.Offset == 0)
            {
                break;
            }
            frames[captured++] = reinterpret_cast<void*>(frame.AddrPC.Offset);
        }
    }
    if (captured == 0)
    {
        captured = ::RtlCaptureStackBackTrace(0, static_cast<DWORD>(MaxFrames), frames, nullptr);
    }
    if (captured == 0)
    {
        emit("  backtrace: unavailable\n");
    }
    else
    {
        emit("  backtrace:\n");
    }

    // SYMBOL_INFO carries a trailing name buffer, so over-allocate in place.
    alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    for (USHORT index = 0; index < captured; ++index)
    {
        const auto address = reinterpret_cast<DWORD64>(frames[index]);
        char line[1024]{};
        IMAGEHLP_LINE64 lineInfo{};
        lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD displacement = 0;
        const bool hasName =
            symbols && ::SymFromAddr(process, address, nullptr, symbol) != FALSE;
        const bool hasLine =
            symbols && ::SymGetLineFromAddr64(process, address, &displacement, &lineInfo) != FALSE;
        if (hasName && hasLine)
        {
            std::snprintf(line, sizeof(line), "    #%02u 0x%016llX %s at %s:%lu\n",
                          static_cast<unsigned>(index), static_cast<unsigned long long>(address),
                          symbol->Name, lineInfo.FileName, lineInfo.LineNumber);
        }
        else if (hasName)
        {
            std::snprintf(line, sizeof(line), "    #%02u 0x%016llX %s\n",
                          static_cast<unsigned>(index), static_cast<unsigned long long>(address),
                          symbol->Name);
        }
        else
        {
            std::snprintf(line, sizeof(line), "    #%02u 0x%016llX\n",
                          static_cast<unsigned>(index), static_cast<unsigned long long>(address));
        }
        emit(line);
    }
    if (symbols)
    {
        (void)::SymCleanup(process);
    }
}
#else
void emitBacktrace(void*) noexcept
{
    if (g_state.captureBacktrace)
    {
        emit("  backtrace: unavailable on this platform\n");
    }
}
#endif

#if defined(_WIN32)
using PlatformContext = CONTEXT;
#else
using PlatformContext = void;
#endif

// The single funnel every handler goes through. `detail` may be null.
//
// The latch is never cleared: one death produces one report. A terminate
// typically ends in abort, and an abort raises SIGABRT, so clearing it would
// print the same crash two or three times and bury the first reason — which is
// the only one that names the actual fault.
void writeReport(const char* reason, const char* detail, PlatformContext* context) noexcept
{
    // Also guards against a crash inside the crash handler: the first thread
    // wins, any other returns and lets the process die.
    if (g_reporting.test_and_set())
    {
        return;
    }

    emit("\n==== Tina fatal error ====\n");
    emitLine("  application: ",
             g_state.applicationName[0] != '\0' ? g_state.applicationName : "(unknown)");
    emitLine("  reason: ", reason);
    if (detail != nullptr && detail[0] != '\0')
    {
        emitLine("  detail: ", detail);
    }
    {
        char line[128]{};
#if defined(_WIN32)
        std::snprintf(line, sizeof(line), "  process: %lu  thread: %lu\n",
                      static_cast<unsigned long>(::GetCurrentProcessId()),
                      static_cast<unsigned long>(::GetCurrentThreadId()));
#else
        std::snprintf(line, sizeof(line), "  process/thread ids unavailable\n");
#endif
        emit(line);
    }
    emitBacktrace(context);
    // Machine-readable trailer so the existing JSON-scraping gates can detect a
    // crash instead of seeing an empty stdout and guessing.
    emit("{\"status\":\"crash\",\"reason\":");
    {
        char quoted[512]{};
        std::snprintf(quoted, sizeof(quoted), "\"%s\"}\n", reason != nullptr ? reason : "");
        emit(quoted);
    }
    emit("==== end Tina fatal error ====\n");

    g_reportCount.fetch_add(1U, std::memory_order_relaxed);
    // Deliberately not cleared; see the note above.
}

// std::terminate covers every std::terminate() call in the engine plus an
// uncaught exception that unwound past main().
void onTerminate() noexcept
{
    // Copied, not borrowed: what() may point into the exception object, which is
    // released when the catch block exits, and the report is written after that.
    char detail[512]{};
    // If a live exception is what got us here, its message is the useful part.
    if (std::current_exception() != nullptr)
    {
        try
        {
            std::rethrow_exception(std::current_exception());
        }
        catch (const std::bad_alloc&)
        {
            copyBounded(detail, sizeof(detail), "std::bad_alloc");
        }
        catch (const std::exception& exception)
        {
            const char* what = exception.what();
            copyBounded(detail, sizeof(detail), what != nullptr ? what : "std::exception");
        }
        catch (...)
        {
            copyBounded(detail, sizeof(detail), "non-standard exception");
        }
    }
    writeReport("std::terminate was called", detail, nullptr);
    if (g_previousTerminate != nullptr && g_previousTerminate != &onTerminate)
    {
        g_previousTerminate();
    }
    std::_Exit(3);
}

extern "C" void onAbortSignal(int) noexcept
{
    writeReport("SIGABRT (abort)", nullptr, nullptr);
    std::_Exit(3);
}

#if defined(_WIN32)
const char* describeSehCode(DWORD code) noexcept
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION (bad pointer dereference)";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW (runaway recursion)";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
        return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "EXCEPTION_DATATYPE_MISALIGNMENT";
    default:
        return "structured exception";
    }
}

LONG WINAPI onUnhandledSeh(EXCEPTION_POINTERS* pointers) noexcept
{
    DWORD code = 0;
    PlatformContext* context = nullptr;
    char detail[256]{};
    if (pointers != nullptr && pointers->ExceptionRecord != nullptr)
    {
        code = pointers->ExceptionRecord->ExceptionCode;
        context = pointers->ContextRecord;
        if (code == EXCEPTION_ACCESS_VIOLATION &&
            pointers->ExceptionRecord->NumberParameters >= 2)
        {
            const ULONG_PTR operation = pointers->ExceptionRecord->ExceptionInformation[0];
            const ULONG_PTR address = pointers->ExceptionRecord->ExceptionInformation[1];
            std::snprintf(detail, sizeof(detail), "%s address 0x%016llX",
                          operation == 0 ? "read from" : (operation == 1 ? "write to" : "execute at"),
                          static_cast<unsigned long long>(address));
        }
        else
        {
            std::snprintf(detail, sizeof(detail), "code 0x%08lX",
                          static_cast<unsigned long>(code));
        }
    }
    writeReport(describeSehCode(code), detail, context);
    return EXCEPTION_EXECUTE_HANDLER;
}

// A pure-virtual call and a CRT invalid parameter both bypass terminate.
void onPureVirtualCall() noexcept
{
    writeReport("pure virtual function call", nullptr, nullptr);
    std::_Exit(3);
}

void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int,
                        uintptr_t) noexcept
{
    writeReport("CRT invalid parameter", nullptr, nullptr);
    std::_Exit(3);
}
#endif

} // namespace

bool installCrashHandler(const CrashHandlerConfig& config) noexcept
{
    copyBounded(g_state.applicationName, MaxNameBytes, config.applicationName);
    copyBounded(g_state.reportPathUtf8, MaxPathBytes, config.reportPathUtf8);
    g_state.captureBacktrace = config.captureBacktrace;

    if (!g_state.installed)
    {
        g_previousTerminate = std::set_terminate(&onTerminate);
        (void)std::signal(SIGABRT, &onAbortSignal);
#if defined(_WIN32)
        ::SetUnhandledExceptionFilter(&onUnhandledSeh);
        _set_purecall_handler(&onPureVirtualCall);
        _set_invalid_parameter_handler(&onInvalidParameter);
        // Without this the CRT pops a modal dialog on abort, which hangs
        // headless gates instead of failing them.
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        g_state.installed = true;
    }
    return true;
}

void uninstallCrashHandler() noexcept
{
    if (!g_state.installed)
    {
        return;
    }
    (void)std::set_terminate(g_previousTerminate);
    (void)std::signal(SIGABRT, SIG_DFL);
#if defined(_WIN32)
    ::SetUnhandledExceptionFilter(nullptr);
#endif
    g_previousTerminate = nullptr;
    g_state.installed = false;
    // Re-arm so a later install can still report; the latch is only meant to
    // collapse the cascade within a single death.
    g_reporting.clear();
}

unsigned long long crashReportCount() noexcept
{
    return g_reportCount.load(std::memory_order_relaxed);
}

void reportFatalAndTerminate(std::string_view reason) noexcept
{
    char buffer[512]{};
    copyBounded(buffer, sizeof(buffer), reason);
    writeReport("fatal engine invariant violated", buffer, nullptr);
    std::_Exit(3);
}

} // namespace Tina::Core::Diagnostics
