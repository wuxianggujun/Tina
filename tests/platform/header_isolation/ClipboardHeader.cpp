#include <tina/platform/Clipboard.hpp>
#include <tina/platform/ProcessLocalClipboard.hpp>

#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

static_assert(std::has_virtual_destructor_v<Tina::Platform::IClipboard>);
static_assert(!std::is_copy_constructible_v<Tina::Platform::IClipboard>);
static_assert(!std::is_move_constructible_v<Tina::Platform::IClipboard>);

static_assert(std::is_same_v<
              decltype(&Tina::Platform::IClipboard::readTextUtf8),
              Tina::Core::Result<Tina::Platform::ClipboardTextRead> (
                  Tina::Platform::IClipboard::*)(std::span<char>)>);
static_assert(std::is_same_v<
              decltype(&Tina::Platform::IClipboard::writeTextUtf8),
              Tina::Core::Status (Tina::Platform::IClipboard::*)(std::string_view)>);

// A default-constructed read is the empty-clipboard answer, not an error, and it
// must not read as "held an empty string": a caller greying out a Paste
// affordance keys off hasText.
static_assert(!Tina::Platform::ClipboardTextRead{}.hasText);
static_assert(Tina::Platform::ClipboardTextRead{}.totalBytes == 0);
static_assert(!Tina::Platform::ClipboardTextRead{}.truncated());

// truncated() compares what was delivered against what exists, so an empty
// destination is a legitimate size query rather than a failed read.
static_assert(Tina::Platform::ClipboardTextRead{
                  .bytesWritten = 0, .totalBytes = 7, .hasText = true}
                  .truncated());
static_assert(!Tina::Platform::ClipboardTextRead{
                  .bytesWritten = 7, .totalBytes = 7, .hasText = true}
                   .truncated());

static_assert(std::is_base_of_v<Tina::Platform::IClipboard, Tina::Platform::ProcessLocalClipboard>);
static_assert(std::is_nothrow_default_constructible_v<Tina::Platform::ProcessLocalClipboard>);
