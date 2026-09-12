#pragma once

#include <tina/platform/Clipboard.hpp>

#include <string>
#include <thread>

namespace Tina::Platform::Detail {

// The GLFW-backed system clipboard.
//
// GLFW's clipboard entry points take a deprecated window argument that accepts
// NULL, so this holds no window handle: what it actually depends on is the GLFW
// library being initialized. It does need to know when the backend has stopped,
// because reaching glfwGetClipboardString after glfwTerminate is undefined
// behaviour rather than a recoverable error.
class GlfwClipboard final : public IClipboard {
  public:
    GlfwClipboard() noexcept : ownerThread_(std::this_thread::get_id()) {}

    [[nodiscard]] Core::Result<ClipboardTextRead> readTextUtf8(
        std::span<char> destination) override;
    [[nodiscard]] Core::Status writeTextUtf8(std::string_view textUtf8) override;

    // Called by the owning backend once GLFW is no longer usable. Every later
    // access fails with BackendStopped instead of entering GLFW.
    void markStopped() noexcept { stopped_ = true; }

  private:
    // Scratch for CRLF expansion. glfwSetClipboardString needs a NUL-terminated
    // string, so the converted text has to be materialized somewhere; reusing
    // one buffer keeps repeated copies from reallocating every time.
    std::string writeScratch_{};
    std::thread::id ownerThread_{};
    bool stopped_ = false;
};

} // namespace Tina::Platform::Detail
