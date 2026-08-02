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
inline constexpr std::string_view StyleStateWorkload = "ui_style_state_v1";

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

[[nodiscard]] std::string extractQuotedField(std::string_view json,
                                             std::string_view field)
{
    const std::string prefix = "\"" + std::string(field) + "\":\"";
    const usize begin = json.find(prefix);
    if (begin == std::string_view::npos)
    {
        return {};
    }
    const usize valueBegin = begin + prefix.size();
    const usize valueEnd = json.find('"', valueBegin);
    return valueEnd == std::string_view::npos
               ? std::string{}
               : std::string(json.substr(valueBegin, valueEnd - valueBegin));
}

[[nodiscard]] std::string runUIWorkload(std::string_view workload,
                                        u64 measureIterations = 1)
{
    std::ostringstream output;
    std::ostringstream errors;
    const int exitCode = runUIBenchmark(workload,
                                        {
                                            .warmUpIterations = 1,
                                            .measureIterations = measureIterations,
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
    EXPECT_TRUE(isUIBenchmarkWorkload(StyleStateWorkload));

    std::ostringstream help;
    printUIBenchmarkHelp(help);
    EXPECT_NE(help.str().find(ComponentPrerequisiteWorkload), std::string::npos);
    EXPECT_NE(help.str().find(ComponentWorkload), std::string::npos);
    EXPECT_NE(help.str().find(StyleStateWorkload), std::string::npos);
}

TEST(UIBenchmarkWorkloadsTests, ComponentPrerequisiteReportsStableHonestSchema)
{
    const std::string first = runUIWorkload(ComponentPrerequisiteWorkload);
    const std::string second = runUIWorkload(ComponentPrerequisiteWorkload);

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
    const std::string first = runUIWorkload(ComponentWorkload);
    const std::string second = runUIWorkload(ComponentWorkload);

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

TEST(UIBenchmarkWorkloadsTests, StyleStateReportsBoundedSingleNodeResolution)
{
    const std::string first = runUIWorkload(StyleStateWorkload, 2);
    const std::string second = runUIWorkload(StyleStateWorkload, 2);

    EXPECT_NE(first.find("\"status\":\"ok\",\"schema\":1"), std::string::npos);
    EXPECT_NE(first.find("\"schemaName\":\"tina_bench\""), std::string::npos);
    EXPECT_NE(first.find("\"id\":\"ui_style_state_v1\""), std::string::npos);
    EXPECT_NE(first.find("\"node_count\":4096"), std::string::npos);
    EXPECT_NE(first.find("\"styled_node_count\":4095"), std::string::npos);
    EXPECT_NE(first.find("\"style_class_count\":64"), std::string::npos);
    EXPECT_NE(first.find("\"style_rule_count\":256"), std::string::npos);
    EXPECT_NE(first.find("\"style_classes_per_node\":4"), std::string::npos);
    EXPECT_NE(first.find("\"style_rules_per_bucket\":4"), std::string::npos);
    EXPECT_NE(first.find("\"style_state\":{\"state_changes\":2,"
                         "\"inspected_nodes\":2,\"resolved_nodes\":2,"
                         "\"candidate_rules\":32,\"clean_commits\":2,"
                         "\"clean_inspected_nodes\":0,\"clean_resolved_nodes\":0,"
                         "\"clean_candidate_rules\":0,\"registered_classes\":64,"
                         "\"active_rules\":256,\"active_buckets\":64,"
                         "\"active_node_class_links\":16380,\"compile_failures\":0,"
                         "\"capacity_failures\":0,\"revision\":1}"),
              std::string::npos);
    EXPECT_NE(first.find("\"style_classes\":64"), std::string::npos);
    EXPECT_NE(first.find("\"style_rules\":256"), std::string::npos);
    EXPECT_NE(first.find("\"style_buckets\":64"), std::string::npos);
    EXPECT_NE(first.find("\"style_rules_per_bucket\":4"), std::string::npos);
    EXPECT_NE(first.find("\"node_style_class_links\":16380"), std::string::npos);
    EXPECT_NE(first.find("\"style_bucket_candidates\":4"), std::string::npos);
    EXPECT_NE(first.find("\"delta\":0"), std::string::npos);
    EXPECT_EQ(first.find("\"style_state\":\"0000000000000000\""),
              std::string::npos);
    const std::string enabledChecksum =
        extractQuotedField(first, "style_enabled_display_list");
    const std::string disabledChecksum =
        extractQuotedField(first, "style_disabled_display_list");
    ASSERT_EQ(enabledChecksum.size(), 16U);
    ASSERT_EQ(disabledChecksum.size(), 16U);
    EXPECT_NE(enabledChecksum, disabledChecksum);

    const std::string firstChecksum = extractChecksum(first);
    const std::string secondChecksum = extractChecksum(second);
    ASSERT_EQ(firstChecksum.size(), 16U);
    EXPECT_EQ(firstChecksum, secondChecksum);
}

} // namespace
} // namespace Tina::Bench
