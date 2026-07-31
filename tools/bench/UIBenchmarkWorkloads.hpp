#pragma once

#include <tina/core/base/Types.hpp>

#include <iosfwd>
#include <string_view>

namespace Tina::Bench {

struct UIBenchmarkOptions final {
    Core::u64 warmUpIterations = 0;
    Core::u64 measureIterations = 0;
    Core::u64 seed = 1;
};

[[nodiscard]] bool isUIBenchmarkWorkload(std::string_view workload) noexcept;

void printUIBenchmarkHelp(std::ostream& output);

// Writes one ADR 0018 schema-v1 JSON record and returns the process exit code.
// The caller has already validated that workload names and numeric options are
// syntactically valid.
[[nodiscard]] int runUIBenchmark(std::string_view workload,
                                 const UIBenchmarkOptions& options,
                                 std::ostream& output,
                                 std::ostream& errors);

} // namespace Tina::Bench
