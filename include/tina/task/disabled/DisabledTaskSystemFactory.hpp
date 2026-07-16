#pragma once

#include <tina/task/TaskSystem.hpp>

namespace Tina::Task {

[[nodiscard]] Core::Result<std::unique_ptr<ITaskSystem>> createDisabledTaskSystem(
    const TaskSystemCreateParams& params);

} // namespace Tina::Task
