#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <string_view>

namespace Tina::Platform {

// Outcome of one clipboard read. A read reports what the clipboard holds and
// how much of it reached the caller's buffer, so a destination that is too
// small is an explicit, recoverable fact rather than silent data loss.
struct ClipboardTextRead final {
    // Bytes written to the caller's destination. A read never splits a UTF-8
    // sequence, so this can stop short of the destination size even when more
    // text remains.
    usize bytesWritten = 0;
    // Total normalized bytes available on the clipboard. Greater than
    // bytesWritten means the destination could not hold everything; the caller
    // decides whether to retry with a larger buffer or accept the prefix.
    // Passing an empty destination is therefore a legitimate size query.
    usize totalBytes = 0;
    // False means the clipboard holds no text at all -- it is empty, or holds
    // only non-text content such as an image. That is distinct both from
    // holding a zero-length string and from a failed read, which returns an
    // error instead. A caller that greys out a Paste affordance wants this,
    // not totalBytes == 0.
    bool hasText = false;

    // True when the clipboard held more text than the destination could take.
    [[nodiscard]] constexpr bool truncated() const noexcept
    {
        return bytesWritten < totalBytes;
    }
};

// The system clipboard, as a capability separate from the backend that owns it.
//
// Reads and writes both speak strict UTF-8 with LF line endings. Normalization
// belongs here rather than in each caller because the native convention is a
// property of the platform: a Windows clipboard carries CRLF, and a UI text
// model that accepts only LF would otherwise reject every paste that came from
// a native editor. Implementations convert on the way in and on the way out, so
// the two directions stay symmetric.
//
// Implementations are thread-affine to the owning backend's owner thread; a
// native clipboard is main-thread-only on the platforms Tina targets.
class IClipboard {
  public:
    virtual ~IClipboard() noexcept = default;

    IClipboard(const IClipboard&) = delete;
    IClipboard& operator=(const IClipboard&) = delete;
    IClipboard(IClipboard&&) = delete;
    IClipboard& operator=(IClipboard&&) = delete;

    // Reads clipboard text into destination as strict UTF-8 with LF line
    // endings, reporting how much was written and how much exists.
    //
    // An empty clipboard is a successful read with hasText == false, not an
    // error. Errors are reserved for reads that could have succeeded and did
    // not: ClipboardUnavailable for a transient refusal such as another process
    // holding the Win32 clipboard lock, and ClipboardTextNotUtf8 when the OS
    // hands over bytes that are not valid UTF-8. Callers must be able to tell
    // "nothing to paste" from "try again", because retrying is right for one
    // and wrong for the other.
    [[nodiscard]] virtual Core::Result<ClipboardTextRead> readTextUtf8(
        std::span<char> destination) = 0;

    // Replaces clipboard content with textUtf8, converting LF to the platform's
    // native line ending. The argument must be strict UTF-8 without embedded
    // NUL; anything else is a caller contract error. Writing an empty string is
    // valid and leaves the clipboard holding empty text.
    [[nodiscard]] virtual Core::Status writeTextUtf8(std::string_view textUtf8) = 0;

  protected:
    IClipboard() noexcept = default;
};

} // namespace Tina::Platform
