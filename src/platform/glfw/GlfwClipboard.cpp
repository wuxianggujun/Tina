#include "GlfwClipboard.hpp"

#include <tina/core/text/Utf8.hpp>
#include <tina/platform/PlatformErrors.hpp>

#include "../ClipboardTextNormalization.hpp"

#include <GLFW/glfw3.h>

#include <new>

namespace Tina::Platform::Detail {
namespace {

void clearPendingGlfwErrors() noexcept
{
    while (glfwGetError(nullptr) != GLFW_NO_ERROR)
    {
    }
}

} // namespace

Core::Result<ClipboardTextRead> GlfwClipboard::readTextUtf8(std::span<char> destination)
{
    if (stopped_)
    {
        return Core::failure(PlatformErrorCode::BackendStopped,
                             "The GLFW platform backend is stopped");
    }
    if (std::this_thread::get_id() != ownerThread_)
    {
        return Core::failure(PlatformErrorCode::WrongOwnerThread,
                             "The GLFW clipboard must be read on the creating thread");
    }

    clearPendingGlfwErrors();
    const char* native = glfwGetClipboardString(nullptr);
    if (native == nullptr)
    {
        // NULL is overloaded: an empty clipboard, content that is not text, a
        // transient failure to take the Win32 global clipboard lock, and an
        // uninitialized library all produce it. Only the native error code
        // separates them, and the distinction matters -- reporting a lost lock
        // race as "nothing to paste" would make a retryable failure look like a
        // deliberate no-op.
        const char* description = nullptr;
        const int code = glfwGetError(&description);
        if (code == GLFW_FORMAT_UNAVAILABLE || code == GLFW_NO_ERROR)
        {
            return ClipboardTextRead{};
        }
        Core::Error error{PlatformErrorCode::ClipboardUnavailable,
                          "The system clipboard refused a read"};
        error.setNativeCode(code);
        error.addContext("glfwGetClipboardString",
                         description != nullptr ? description : "GLFW did not provide a native error");
        return Core::failure(std::move(error));
    }
    clearPendingGlfwErrors();

    const std::string_view text{native};
    // GLFW documents its result as UTF-8, but this is OS-supplied data crossing
    // a process boundary and every consumer downstream treats strict UTF-8 as an
    // invariant. Validate here rather than trust the promise.
    if (!Core::isStrictUtf8WithoutNul(text))
    {
        return Core::failure(PlatformErrorCode::ClipboardTextNotUtf8,
                             "The system clipboard holds text that is not strict UTF-8");
    }

    const ClipboardNormalizeResult normalized =
        normalizeClipboardTextToLf(text, destination);
    return ClipboardTextRead{
        .bytesWritten = normalized.bytesWritten,
        .totalBytes = normalized.totalBytes,
        .hasText = true,
    };
}

Core::Status GlfwClipboard::writeTextUtf8(std::string_view textUtf8)
{
    if (stopped_)
    {
        return Core::failure(PlatformErrorCode::BackendStopped,
                             "The GLFW platform backend is stopped");
    }
    if (std::this_thread::get_id() != ownerThread_)
    {
        return Core::failure(PlatformErrorCode::WrongOwnerThread,
                             "The GLFW clipboard must be written on the creating thread");
    }
    if (!Core::isStrictUtf8WithoutNul(textUtf8))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Clipboard text must be strict UTF-8 without embedded NUL");
    }

    const usize requiredBytes = clipboardTextSizeWithCrlf(textUtf8);
    try
    {
        writeScratch_.resize(requiredBytes);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Clipboard write scratch allocation failed");
    }
    if (requiredBytes != 0)
    {
        const usize written = expandClipboardTextToCrlf(
            textUtf8, std::span<char>{writeScratch_.data(), writeScratch_.size()});
        if (written != requiredBytes)
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "Clipboard CRLF expansion produced an unexpected length");
        }
    }

    clearPendingGlfwErrors();
    glfwSetClipboardString(nullptr, writeScratch_.c_str());
    const char* description = nullptr;
    const int code = glfwGetError(&description);
    if (code != GLFW_NO_ERROR)
    {
        Core::Error error{PlatformErrorCode::ClipboardUnavailable,
                          "The system clipboard refused a write"};
        error.setNativeCode(code);
        error.addContext("glfwSetClipboardString",
                         description != nullptr ? description : "GLFW did not provide a native error");
        return Core::failure(std::move(error));
    }
    return Core::success();
}

} // namespace Tina::Platform::Detail
