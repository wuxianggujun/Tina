#pragma once

#include <tina/platform/Clipboard.hpp>

#include <string>

namespace Tina::Platform {

// An IClipboard that stores text in this process and nowhere else.
//
// This is what Headless returns. Headless has no OS clipboard, but refusing the
// capability outright would mean every test covering copy/paste has to supply
// its own fake, so the behaviour under test would be a different implementation
// in each one. A single deterministic implementation is the honest way to make
// the capability testable without pretending the OS is involved.
//
// It is public for the same reason: tests, Headless, and an embedder driving Tina
// without a desktop backend should share one implementation rather than write
// three. Content is never visible to other processes.
//
// Text is held as strict UTF-8 with LF endings -- the same form IClipboard hands
// out -- so no line-ending conversion happens in either direction. Reads still
// stop on a UTF-8 sequence boundary when the destination is too small.
class ProcessLocalClipboard final : public IClipboard {
  public:
    ProcessLocalClipboard() noexcept = default;

    [[nodiscard]] Core::Result<ClipboardTextRead> readTextUtf8(
        std::span<char> destination) override;
    [[nodiscard]] Core::Status writeTextUtf8(std::string_view textUtf8) override;

    // True once anything has been written. Distinguishes a clipboard holding an
    // empty string from one that was never written, matching the hasText
    // distinction a native clipboard reports.
    [[nodiscard]] bool hasText() const noexcept { return hasText_; }

    // Drops any stored text and returns to the never-written state, so a test
    // can assert the empty-clipboard path without constructing a new instance.
    void clear() noexcept;

  private:
    std::string text_{};
    bool hasText_ = false;
};

} // namespace Tina::Platform
