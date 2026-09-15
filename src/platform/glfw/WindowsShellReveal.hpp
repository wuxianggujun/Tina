#pragma once

#include <tina/platform/ShellReveal.hpp>

#include <thread>

namespace Tina::Platform::Detail {

class WindowsShellReveal final : public IShellReveal {
  public:
    WindowsShellReveal() noexcept : ownerThread_(std::this_thread::get_id()) {}

    [[nodiscard]] Core::Status revealPath(std::string_view utf8AbsolutePath) override;

  private:
    std::thread::id ownerThread_{};
};

} // namespace Tina::Platform::Detail
