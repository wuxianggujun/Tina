#include "UIBenchmarkWorkloads.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <string_view>

namespace Tina::Bench {
namespace {

inline constexpr std::string_view ComponentPrerequisiteWorkload =
    "ui_component_build_activate_toggle_v1";
inline constexpr std::string_view ComponentWorkload = "ui_component_build_v1";

[[nodiscard]] std::string extractChecksum(std::string_view json)
{
    constexpr std::string_view Prefix = ",\"checksum\":\"";
    const usize begin = json.rfind(Prefix);
    if (begin == std::string_view::npos)
    {
        return {};
    }
    const usize valueBegin = begin + Prefix.size();
    const usize valueEnd = json.find('"', valueBegin);
    if (valueEnd == std::string_view::npos)
    {
        return {};
    }
    return std::string(json.substr(valueBegin, valueEnd - valueBegin));
}

[[nodiscard]] std::string runComponentWorkload(std::string_view workload)
{
    std::ostringstream output;
    std::ostringstream errors;
    const int exitCode = runUIBenchmark(workload,
                                        {
                                            .warmUpIterations = 1,
                                            .measureIterations = 1,
                                            .seed = 7,
                                        },
                                        output, errors);
    EXPECT_EQ(exitCode, 0) << errors.str();
    EXPECT_TRUE(errors.str().empty());
    return output.str();
}

TEST(UIBenchmarkWorkloadsTests, RegistersPrerequisiteAndFrozenComponentWorkloads)
{
    EXPECT_TRUE(isUIBenchmarkWorkload(ComponentPrerequisiteWorkload));
    EXPECT_TRUE(isUIBenchmarkWorkload(ComponentWorkload));

    std::ostringstream help;
    printUIBenchmarkHelp(help);
    EXPECT_NE(help.str().find(ComponentPrerequisiteWorkload), std::string::npos);
    EXPECT_NE(help.str().find(ComponentWorkload), std::string::npos);
}

TEST(UIBenchmarkWorkloadsTests, ComponentPrerequisiteReportsStableHonestSchema)
{
    const std::string first = runComponentWorkload(ComponentPrerequisiteWorkload);
    const std::string second = runComponentWorkload(ComponentPrerequisiteWorkload);

    EXPECT_NE(first.find("\"status\":\"ok\",\"schema\":1"), std::string::npos);
    EXPECT_NE(first.find("\"schemaName\":\"tina_bench\""), std::string::npos);
    EXPECT_NE(first.find("\"id\":\"ui_component_build_activate_toggle_v1\""), std::string::npos);
    EXPECT_NE(first.find("\"component_count\":256"), std::string::npos);
    EXPECT_NE(first.find("\"component_nodes_per_transaction\":4"), std::string::npos);
    EXPECT_NE(first.find("\"component_text_bytes_per_transaction\":11"), std::string::npos);
    EXPECT_NE(first.find("\"component_canvas_commands_per_transaction\":2"), std::string::npos);
    EXPECT_NE(first.find("\"coverage\":\"activate_toggle_only\""), std::string::npos);
    EXPECT_NE(first.find("\"transaction_scope\":\"UIElementBuildTransaction\""), std::string::npos);
    EXPECT_NE(first.find("\"frozen_workload_complete\":false"), std::string::npos);
    EXPECT_NE(first.find("\"reservation_counters_available\":false"), std::string::npos);
    EXPECT_NE(first.find("\"transactions_started\":256"), std::string::npos);
    EXPECT_NE(first.find("\"transactions_committed\":256"), std::string::npos);
    EXPECT_NE(first.find("\"nodes_requested\":1024,\"nodes_published\":1024"), std::string::npos);
    EXPECT_NE(first.find("\"clean_commits\":1,\"clean_commit_rebuilds\":0"), std::string::npos);
    EXPECT_NE(first.find("\"range\":{\"supported\":false}"), std::string::npos);
    EXPECT_NE(first.find("\"selection\":{\"supported\":false}"), std::string::npos);
    EXPECT_NE(first.find("\"activate_behavior\":512"), std::string::npos);
    EXPECT_NE(first.find("\"toggle_behavior\":512"), std::string::npos);
    EXPECT_NE(first.find("\"delta\":0"), std::string::npos);
    EXPECT_EQ(first.find("\"component_tree\":\"0000000000000000\""), std::string::npos);

    const std::string firstChecksum = extractChecksum(first);
    const std::string secondChecksum = extractChecksum(second);
    ASSERT_EQ(firstChecksum.size(), 16U);
    EXPECT_EQ(firstChecksum, secondChecksum);
}

TEST(UIBenchmarkWorkloadsTests, FrozenComponentWorkloadReportsRealReservationCounters)
{
    const std::string first = runComponentWorkload(ComponentWorkload);
    const std::string second = runComponentWorkload(ComponentWorkload);

    EXPECT_NE(first.find("\"status\":\"ok\",\"schema\":1"), std::string::npos);
    EXPECT_NE(first.find("\"schemaName\":\"tina_bench\""), std::string::npos);
    EXPECT_NE(first.find("\"id\":\"ui_component_build_v1\""), std::string::npos);
    EXPECT_NE(first.find("\"component_count\":256"), std::string::npos);
    EXPECT_NE(first.find("\"component_nodes_per_transaction\":4"), std::string::npos);
    EXPECT_NE(first.find("\"component_text_bytes_per_transaction\":11"), std::string::npos);
    EXPECT_NE(first.find("\"component_canvas_commands_per_transaction\":2"), std::string::npos);
    EXPECT_NE(first.find("\"component_activate_slots_per_transaction\":2"), std::string::npos);
    EXPECT_NE(first.find("\"component_toggle_slots_per_transaction\":1"), std::string::npos);
    EXPECT_NE(first.find("\"component_range_slots_per_transaction\":1"), std::string::npos);
    EXPECT_NE(first.find("\"component_text_input_slots_per_transaction\":1"), std::string::npos);
    EXPECT_NE(first.find("\"component_scroll_slots_per_transaction\":1"), std::string::npos);
    EXPECT_NE(first.find("\"component_selection_slots_per_transaction\":1"), std::string::npos);
    EXPECT_NE(first.find("\"coverage\":\"all_reserved_pools\""), std::string::npos);
    EXPECT_NE(first.find("\"frozen_workload_complete\":true"), std::string::npos);
    EXPECT_NE(first.find("\"reservation_counters_available\":true"), std::string::npos);
    EXPECT_NE(first.find("\"transactions_started\":256"), std::string::npos);
    EXPECT_NE(first.find("\"transactions_committed\":256"), std::string::npos);
    EXPECT_NE(first.find("\"clean_commits\":1,\"clean_commit_rebuilds\":0"),
              std::string::npos);
    EXPECT_NE(first.find("\"active_transactions\":0,\"transaction_failures\":0"),
              std::string::npos);
    EXPECT_NE(first.find("\"nodes\":{\"requested\":1024,\"reserved\":1024,"
                         "\"published\":1024,\"capacity_failures\":0,\"outstanding\":0}"),
              std::string::npos);
    EXPECT_NE(first.find("\"text_bytes\":{\"requested\":2816,\"reserved\":2816,"
                         "\"published\":2816,\"capacity_failures\":0,\"outstanding\":0}"),
              std::string::npos);
    EXPECT_NE(first.find("\"canvas_commands\":{\"requested\":512,\"reserved\":512,"
                         "\"published\":512,\"capacity_failures\":0,\"outstanding\":0}"),
              std::string::npos);
    EXPECT_NE(first.find("\"activate\":{\"requested\":512,\"reserved\":512,"
                         "\"published\":512,\"capacity_failures\":0,\"outstanding\":0}"),
              std::string::npos);
    for (const std::string_view pool : {"toggle", "range", "text_input", "scroll", "selection"})
    {
        const std::string expected = "\"" + std::string(pool) +
                                     "\":{\"requested\":256,\"reserved\":256,"
                                     "\"published\":256,\"capacity_failures\":0,"
                                     "\"outstanding\":0}";
        EXPECT_NE(first.find(expected), std::string::npos) << pool;
    }
    EXPECT_NE(first.find("\"activate_behavior\":512"), std::string::npos);
    EXPECT_NE(first.find("\"toggle_behavior\":256"), std::string::npos);
    EXPECT_NE(first.find("\"range_behavior\":256"), std::string::npos);
    EXPECT_NE(first.find("\"text_input_behavior\":256"), std::string::npos);
    EXPECT_NE(first.find("\"scroll_behavior\":256"), std::string::npos);
    EXPECT_NE(first.find("\"selection_behavior\":256"), std::string::npos);
    EXPECT_NE(first.find("\"delta\":0"), std::string::npos);
    EXPECT_EQ(first.find("\"component_tree\":\"0000000000000000\""), std::string::npos);

    const std::string firstChecksum = extractChecksum(first);
    const std::string secondChecksum = extractChecksum(second);
    ASSERT_EQ(firstChecksum.size(), 16U);
    EXPECT_EQ(firstChecksum, secondChecksum);
}

} // namespace
} // namespace Tina::Bench
