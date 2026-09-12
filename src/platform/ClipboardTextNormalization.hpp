#pragma once

// Line-ending normalization shared by every clipboard implementation. The
// conversion is identical on all of them and the failure modes are subtle
// enough -- lone CR, a CRLF pair straddling the truncation point, truncation
// landing inside a UTF-8 sequence -- that duplicating it per backend would mean
// duplicating the bugs.

#include <tina/core/base/Types.hpp>

#include <span>
#include <string_view>

namespace Tina::Platform::Detail {

struct ClipboardNormalizeResult final {
    usize bytesWritten = 0;
    // Bytes the fully normalized text occupies, independent of what fit.
    usize totalBytes = 0;
};

[[nodiscard]] constexpr bool isUtf8ContinuationByte(char value) noexcept
{
    return (static_cast<unsigned char>(value) & 0xC0U) == 0x80U;
}

// Converts CRLF and lone CR to LF, writing as much as fits in destination.
//
// Both forms collapse to LF because the UI text model accepts LF only and
// treats any CR as grounds to reject the whole insertion: leaving a lone CR in
// place would turn a paste from an old-Mac-era file into a silent no-op.
//
// Output stops on a UTF-8 sequence boundary, and never between the CR and LF of
// a pair, so a truncated read is always well-formed text rather than a
// half-decoded scalar. totalBytes always reports the full normalized length so
// the caller can size a second buffer.
[[nodiscard]] constexpr ClipboardNormalizeResult normalizeClipboardTextToLf(
    std::string_view source, std::span<char> destination) noexcept
{
    ClipboardNormalizeResult result{};
    bool destinationFull = false;
    for (usize index = 0; index < source.size();)
    {
        const char current = source[index];
        usize consumed = 1;
        char emitted = current;
        if (current == '\r')
        {
            // Consume the LF of a CRLF pair together with its CR, so the pair
            // can never be split across two reads.
            if (index + 1 < source.size() && source[index + 1] == '\n')
            {
                consumed = 2;
            }
            emitted = '\n';
        }

        if (!destinationFull)
        {
            // A continuation byte only fits if its lead byte was written, which
            // is implied: bytes are emitted in order and a stall latches.
            if (result.bytesWritten < destination.size())
            {
                destination[result.bytesWritten] = emitted;
                ++result.bytesWritten;
            }
            else
            {
                destinationFull = true;
            }
        }
        ++result.totalBytes;
        index += consumed;
    }

    // Retreat off a UTF-8 sequence that was cut short. Only the trailing
    // sequence can be incomplete, and only when output stopped early.
    //
    // Presence of continuation bytes is not itself evidence of truncation: a
    // destination that ends exactly on a complete multi-byte scalar also ends on
    // a continuation byte. So measure the trailing sequence against the length
    // its lead byte declares and retreat only when it falls short, otherwise a
    // buffer ending on a whole scalar would lose it.
    if (result.bytesWritten < result.totalBytes)
    {
        usize sequenceStart = result.bytesWritten;
        while (sequenceStart > 0 &&
               isUtf8ContinuationByte(destination[sequenceStart - 1]))
        {
            --sequenceStart;
        }
        if (sequenceStart > 0)
        {
            const auto lead =
                static_cast<unsigned char>(destination[sequenceStart - 1]);
            usize declaredLength = 1;
            if ((lead & 0xF8U) == 0xF0U) { declaredLength = 4; }
            else if ((lead & 0xF0U) == 0xE0U) { declaredLength = 3; }
            else if ((lead & 0xE0U) == 0xC0U) { declaredLength = 2; }
            const usize presentLength = result.bytesWritten - (sequenceStart - 1);
            if (declaredLength > 1 && presentLength < declaredLength)
            {
                result.bytesWritten = sequenceStart - 1;
            }
        }
    }
    return result;
}

// Bytes needed to express LF-normalized text with CRLF line endings.
[[nodiscard]] constexpr usize clipboardTextSizeWithCrlf(std::string_view textLf) noexcept
{
    usize size = textLf.size();
    for (const char value : textLf)
    {
        if (value == '\n')
        {
            ++size;
        }
    }
    return size;
}

// Expands LF to CRLF into destination, which must hold at least
// clipboardTextSizeWithCrlf(textLf) bytes. Returns bytes written, or 0 when the
// destination is too small -- a partial CRLF expansion is never published.
[[nodiscard]] constexpr usize expandClipboardTextToCrlf(
    std::string_view textLf, std::span<char> destination) noexcept
{
    if (destination.size() < clipboardTextSizeWithCrlf(textLf))
    {
        return 0;
    }
    usize written = 0;
    for (const char value : textLf)
    {
        if (value == '\n')
        {
            destination[written] = '\r';
            ++written;
        }
        destination[written] = value;
        ++written;
    }
    return written;
}

} // namespace Tina::Platform::Detail
