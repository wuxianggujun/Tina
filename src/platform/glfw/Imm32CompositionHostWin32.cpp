#include "Imm32CompositionHostWin32.hpp"

#include <tina/platform/PlatformErrors.hpp>

#include <imm.h>

#include <cstring>

#pragma comment(lib, "imm32.lib")

namespace Tina::Platform::Detail {
namespace {

constexpr const wchar_t* kHostPropName = L"Tina.Imm32CompositionHost";

[[nodiscard]] Imm32CompositionHostWin32* hostFromHwnd(HWND hwnd) noexcept
{
    return static_cast<Imm32CompositionHostWin32*>(GetPropW(hwnd, kHostPropName));
}

} // namespace

Core::Status Imm32CompositionHostWin32::attach(HWND hwnd) noexcept
{
    if (hwnd == nullptr) {
        return Core::failure(
            PlatformErrorCode::WindowSurfaceUnavailable,
            "IMM32 composition host requires a valid HWND");
    }
    if (hwnd_ != nullptr) {
        return Core::failure(
            PlatformErrorCode::BackendOperationFailed,
            "IMM32 composition host is already attached");
    }

    SetLastError(0);
    if (SetPropW(hwnd, kHostPropName, this) == FALSE) {
        return Core::failure(
            PlatformErrorCode::BackendOperationFailed,
            "IMM32 composition host could not store the HWND property");
    }

    SetLastError(0);
    const LONG_PTR previous = SetWindowLongPtrW(
        hwnd,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&imm32CompositionSubclassProc));
    if (previous == 0 && GetLastError() != 0) {
        RemovePropW(hwnd, kHostPropName);
        return Core::failure(
            PlatformErrorCode::BackendOperationFailed,
            "IMM32 composition host could not subclass the HWND");
    }

    hwnd_ = hwnd;
    previousProc_ = reinterpret_cast<WNDPROC>(previous);
    return Core::success();
}

void Imm32CompositionHostWin32::detach() noexcept
{
    if (hwnd_ == nullptr) {
        return;
    }
    if (previousProc_ != nullptr) {
        SetWindowLongPtrW(
            hwnd_,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(previousProc_));
    }
    RemovePropW(hwnd_, kHostPropName);
    static_cast<void>(session_.cancel());
    clearPending();
    textInputPlacement_.reset();
    hwnd_ = nullptr;
    previousProc_ = nullptr;
}

void Imm32CompositionHostWin32::setTextInputPlacement(
    std::optional<GlfwTextInputPlacementPixels> placement) noexcept
{
    textInputPlacement_ = placement;
    if (hwnd_ == nullptr)
    {
        return;
    }
    const HIMC context = ImmGetContext(hwnd_);
    if (context != nullptr)
    {
        applyTextInputPlacement(context);
        ImmReleaseContext(hwnd_, context);
    }
}

void Imm32CompositionHostWin32::applyTextInputPlacement(HIMC context) noexcept
{
    if (context == nullptr)
    {
        return;
    }
    if (!textInputPlacement_.has_value())
    {
        // Restore IMM32's default composition/candidate policy when the
        // retained UI caret is no longer publishable (focus loss, hidden
        // window, or a failed paint commit).  Merely dropping the cached
        // rectangle leaves an already-open candidate window at the old point.
        CANDIDATEFORM candidate{};
        candidate.dwIndex = 0;
        candidate.dwStyle = CFS_DEFAULT;
        static_cast<void>(ImmSetCandidateWindow(context, &candidate));

        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_DEFAULT;
        static_cast<void>(ImmSetCompositionWindow(context, &composition));
        return;
    }
    const GlfwTextInputPlacementPixels& placement = *textInputPlacement_;
    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = POINT{
        .x = placement.candidateX,
        .y = placement.candidateY,
    };
    static_cast<void>(ImmSetCandidateWindow(context, &candidate));

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = POINT{
        .x = placement.caretLeft,
        .y = placement.caretTop,
    };
    static_cast<void>(ImmSetCompositionWindow(context, &composition));
}

std::optional<Imm32CompositionHostWin32::Pending>
Imm32CompositionHostWin32::takePending() noexcept
{
    if (pendingEventCount_ == 0U) {
        return std::nullopt;
    }
    Pending out = std::move(pendingEvents_[0]);
    for (usize index = 1; index < pendingEventCount_; ++index) {
        pendingEvents_[index - 1U] = std::move(pendingEvents_[index]);
    }
    --pendingEventCount_;
    pendingEvents_[pendingEventCount_] = {};
    // Rebind string_views to the moved storage.
    out.composition.preeditUtf8 = out.preeditStorage;
    out.composition.committedUtf8 = out.commitStorage;
    return out;
}

std::optional<Imm32CompositionEvent>
Imm32CompositionHostWin32::onFocusLost() noexcept
{
    clearPending();
    return session_.onFocusLost();
}

void Imm32CompositionHostWin32::publish(
    Imm32CompositionEvent event,
    std::string preedit,
    std::string commit) noexcept
{
    if (event.stage == TextCompositionStage::Updated &&
        pendingEventCount_ != 0U) {
        Pending& previous = pendingEvents_[pendingEventCount_ - 1U];
        if (previous.composition.stage == TextCompositionStage::Started ||
            previous.composition.stage == TextCompositionStage::Updated) {
            event.stage = previous.composition.stage;
            previous.preeditStorage = std::move(preedit);
            previous.commitStorage = std::move(commit);
            previous.composition = event;
            return;
        }
    }

    usize slot = pendingEventCount_;
    if (pendingEventCount_ < pendingEvents_.size()) {
        ++pendingEventCount_;
    } else if (event.stage == TextCompositionStage::Started ||
               event.stage == TextCompositionStage::Updated) {
        return;
    } else {
        for (usize index = 1; index < pendingEventCount_; ++index) {
            pendingEvents_[index - 1U] = std::move(pendingEvents_[index]);
        }
        slot = pendingEventCount_ - 1U;
    }
    Pending& pending = pendingEvents_[slot];
    pending.preeditStorage = std::move(preedit);
    pending.commitStorage = std::move(commit);
    pending.composition = event;
}

void Imm32CompositionHostWin32::clearPending() noexcept
{
    for (usize index = 0; index < pendingEventCount_; ++index) {
        pendingEvents_[index] = {};
    }
    pendingEventCount_ = 0U;
}

bool Imm32CompositionHostWin32::readImmString(
    HIMC context,
    DWORD index,
    std::string& outUtf8) noexcept
{
    outUtf8.clear();
    if (context == nullptr) {
        return false;
    }
    const LONG wideBytes = ImmGetCompositionStringW(context, index, nullptr, 0);
    if (wideBytes < 0) {
        return false;
    }
    if (wideBytes == 0) {
        return true;
    }
    const usize wideChars = static_cast<usize>(wideBytes) / sizeof(wchar_t);
    if (wideChars > wideScratch_.size()) {
        return false;
    }
    const LONG written = ImmGetCompositionStringW(
        context,
        index,
        wideScratch_.data(),
        static_cast<DWORD>(wideBytes));
    if (written <= 0) {
        return false;
    }
    const usize count = static_cast<usize>(written) / sizeof(wchar_t);
    std::array<char16_t, DefaultImePreeditByteCapacity> utf16{};
    for (usize i = 0; i < count; ++i) {
        utf16[i] = static_cast<char16_t>(wideScratch_[i]);
    }
    std::array<char, DefaultImePreeditByteCapacity> utf8{};
    const auto utf8Count =
        encodeUtf16ToUtf8(std::span<const char16_t>(utf16.data(), count), utf8);
    if (!utf8Count.has_value()) {
        return false;
    }
    outUtf8.assign(utf8.data(), *utf8Count);
    return true;
}

LRESULT Imm32CompositionHostWin32::subclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) noexcept
{
    if (message == WM_IME_COMPOSITION) {
        const HIMC context = ImmGetContext(hwnd);
        if (context != nullptr) {
            if ((lParam & GCS_RESULTSTR) != 0) {
                std::string commit;
                if (readImmString(context, GCS_RESULTSTR, commit)) {
                    auto ended = session_.end(commit);
                    if (ended.has_value()) {
                        publish(*ended, {}, commit);
                    }
                }
                ImmReleaseContext(hwnd, context);
                return 0;
            }
            if ((lParam & GCS_COMPSTR) != 0) {
                std::string preedit;
                if (readImmString(context, GCS_COMPSTR, preedit)) {
                    u32 cursor = 0;
                    if ((lParam & GCS_CURSORPOS) != 0) {
                        const LONG cursorPos =
                            ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
                        if (cursorPos >= 0) {
                            cursor = static_cast<u32>(cursorPos);
                        }
                    }
                    auto updated = session_.updatePreedit(preedit, cursor);
                    if (updated) {
                        publish(*updated, preedit, {});
                    }
                }
                ImmReleaseContext(hwnd, context);
                return 0;
            }
            ImmReleaseContext(hwnd, context);
        }
    } else if (message == WM_IME_ENDCOMPOSITION) {
        auto ended = session_.end();
        if (ended.has_value()) {
            publish(*ended, {}, {});
        }
        return 0;
    } else if (message == WM_IME_STARTCOMPOSITION) {
        const HIMC context = ImmGetContext(hwnd);
        if (context != nullptr) {
            applyTextInputPlacement(context);
            ImmReleaseContext(hwnd, context);
        }
        // First GCS_COMPSTR will emit Started via updatePreedit.
        return 0;
    }

    if (previousProc_ != nullptr) {
        return CallWindowProcW(previousProc_, hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK imm32CompositionSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (Imm32CompositionHostWin32* host = hostFromHwnd(hwnd); host != nullptr) {
        return host->subclassProc(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace Tina::Platform::Detail
