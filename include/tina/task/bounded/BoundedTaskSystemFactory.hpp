#pragma once

#include <tina/task/TaskSystem.hpp>

namespace Tina::Task {

// ADR 0017: bounded IO workers + optional CPU workers + owner-thread Main completion queue.
// TaskGroup is a separate type over scheduleCpu. No priorities/fiber/work stealing in this slice.
[[nodiscard]] Core::Result<std::unique_ptr<ITaskSystem>> createBoundedTaskSystem(const TaskSystemCreateParams& params);

} // namespace Tina::Task
