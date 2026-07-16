#pragma once

#include <tina/core/error/Result.hpp>

#include <functional>
#include <memory>

namespace Tina::Task {

struct TaskSystemCreateParams final {
};

class ITaskSystem {
public:
    virtual ~ITaskSystem() = default;

    [[nodiscard]] virtual bool isIdle() const noexcept = 0;
    virtual void shutdownAndJoin() noexcept = 0;
};

using TaskSystemFactory = std::move_only_function<
    Core::Result<std::unique_ptr<ITaskSystem>>(const TaskSystemCreateParams&)>;

} // namespace Tina::Task
