#include <tina/platform/ProcessLocalClipboard.hpp>

#include <tina/core/text/Utf8.hpp>

#include "ClipboardTextNormalization.hpp"

#include <new>

namespace Tina::Platform {

Core::Result<ClipboardTextRead> ProcessLocalClipboard::readTextUtf8(std::span<char> destination)
{
    if (!hasText_)
    {
        return ClipboardTextRead{};
    }
    // Stored text is already LF-normalized, so this call is only doing
    // boundary-safe truncation. Routing it through the same helper as the native
    // backends keeps one definition of where a read is allowed to stop.
    const Detail::ClipboardNormalizeResult normalized =
        Detail::normalizeClipboardTextToLf(text_, destination);
    return ClipboardTextRead{
        .bytesWritten = normalized.bytesWritten,
        .totalBytes = normalized.totalBytes,
        .hasText = true,
    };
}

Core::Status ProcessLocalClipboard::writeTextUtf8(std::string_view textUtf8)
{
    if (!Core::isStrictUtf8WithoutNul(textUtf8))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Clipboard text must be strict UTF-8 without embedded NUL");
    }
    // Normalize on the way in even though callers owe LF text: this is the only
    // clipboard a Headless product ever sees, and letting a stray CR through
    // would make it the one implementation whose reads violate the LF contract.
    const usize requiredBytes =
        Detail::normalizeClipboardTextToLf(textUtf8, {}).totalBytes;
    std::string normalized;
    try
    {
        normalized.resize(requiredBytes);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Clipboard write allocation failed");
    }
    if (requiredBytes != 0)
    {
        const Detail::ClipboardNormalizeResult written = Detail::normalizeClipboardTextToLf(
            textUtf8, std::span<char>{normalized.data(), normalized.size()});
        if (written.bytesWritten != requiredBytes)
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "Clipboard normalization produced an unexpected length");
        }
    }
    text_ = std::move(normalized);
    hasText_ = true;
    return Core::success();
}

void ProcessLocalClipboard::clear() noexcept
{
    text_.clear();
    hasText_ = false;
}

} // namespace Tina::Platform
