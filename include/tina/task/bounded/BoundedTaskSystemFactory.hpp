#pragma once

#include <tina/task/TaskSystem.hpp>

namespace Tina::Task {

// First ADR 0017 slice: 1..N IO workers + owner-thread Main completion queue.
// No CPU worker pool, no TaskGroup, no priorities in this slice.
[[nodiscard]] Core::Result<std::unique_ptr<ITaskSystem>> createBoundedTaskSystem(const TaskSystemCreateParams& params);

} // namespace Tina::Task
