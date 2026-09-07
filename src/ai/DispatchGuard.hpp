#pragma once

namespace Tina::AI::Detail {
class DispatchGuard final {
public:
    explicit DispatchGuard(bool& flag) noexcept : m_flag(flag) { m_flag = true; }
    ~DispatchGuard() noexcept { m_flag = false; }
    DispatchGuard(const DispatchGuard&) = delete;
    DispatchGuard& operator=(const DispatchGuard&) = delete;
private:
    bool& m_flag;
};
} // namespace Tina::AI::Detail
