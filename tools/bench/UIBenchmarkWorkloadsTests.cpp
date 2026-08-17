#include "UIBenchmarkWorkloads.hpp"

#include <gtest/gtest.h>

#include <array>
#include <sstream>
#include <string>
#include <string_view>

namespace Tina::Bench {
namespace {

inline constexpr std::string_view ComponentPrerequisiteWorkload =
    "ui_component_build_activate_toggle_v1";
inline constexpr std::string_view ComponentWorkload = "ui_component_build_v1";
inline constexpr std::string_view StyleStateWorkload = "ui_style_state_v1";
inline constexpr std::string_view MotionWorkload = "ui_motion_v1";
inline constexpr std::string_view TimelineMotionWorkload = "ui_motion_timeline_v1";
inline constexpr std::string_view LayoutTimelineMotionWorkload = "ui_motion_layout_v1";

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
                                        u64 measureIterations = 1, u64 seed = 7)
{
    std::ostringstream output;
    std::ostringstream errors;
    const int exitCode = runUIBenchmark(workload,
                                        {
                                            .warmUpIterations = 1,
                                            .measureIterations = measureIterations,
                                            .seed = seed,
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
    EXPECT_TRUE(isUIBenchmarkWorkload(MotionWorkload));
    EXPECT_TRUE(isUIBenchmarkWorkload(TimelineMotionWorkload));
    EXPECT_TRUE(isUIBenchmarkWorkload(LayoutTimelineMotionWorkload));

    std::ostringstream help;
    printUIBenchmarkHelp(help);
    EXPECT_NE(help.str().find(ComponentPrerequisiteWorkload), std::string::npos);
    EXPECT_NE(help.str().find(ComponentWorkload), std::string::npos);
    EXPECT_NE(help.str().find(StyleStateWorkload), std::string::npos);
    EXPECT_NE(help.str().find(MotionWorkload), std::string::npos);
    EXPECT_NE(help.str().find(TimelineMotionWorkload), std::string::npos);
    EXPECT_NE(help.str().find(LayoutTimelineMotionWorkload), std::string::npos);
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
                         "\"registered_tokens\":0,\"active_rules\":256,"
                         "\"active_buckets\":64,"
                         "\"active_node_class_links\":16380,\"compile_failures\":0,"
                         "\"capacity_failures\":0,\"revision\":1}"),
              std::string::npos);
    EXPECT_NE(first.find("\"style_classes\":64"), std::string::npos);
    EXPECT_NE(first.find("\"style_tokens\":64"), std::string::npos);
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

TEST(UIBenchmarkWorkloadsTests, MotionZeroActiveAddsNoExtraDirty)
{
    // seed % 3 == 0 → 0 active tracks
    const std::string first = runUIWorkload(MotionWorkload, 2, 0);
    const std::string second = runUIWorkload(MotionWorkload, 2, 0);

    EXPECT_NE(first.find("\"status\":\"ok\",\"schema\":1"), std::string::npos);
    EXPECT_NE(first.find("\"id\":\"ui_motion_v1\""), std::string::npos);
    EXPECT_NE(first.find("\"active_motion_tracks\":0"), std::string::npos);
    EXPECT_NE(first.find("\"motion_track_capacity\":1024"), std::string::npos);
    EXPECT_NE(first.find("\"zero_active_iterations\":2"), std::string::npos);
    EXPECT_NE(first.find("\"sampled_tracks\":0"), std::string::npos);
    EXPECT_NE(first.find("\"delta\":0"), std::string::npos);

    const std::string firstChecksum = extractChecksum(first);
    const std::string secondChecksum = extractChecksum(second);
    ASSERT_EQ(firstChecksum.size(), 16U);
    EXPECT_EQ(firstChecksum, secondChecksum);
}

TEST(UIBenchmarkWorkloadsTests, TimelineMotionZeroActiveKeepsCleanSnapshots)
{
    // seed % 3 == 0 -> zero active tracks/timelines.
    const std::string first = runUIWorkload(TimelineMotionWorkload, 2, 0);
    const std::string second = runUIWorkload(TimelineMotionWorkload, 2, 0);

    EXPECT_NE(first.find("\"status\":\"ok\",\"schema\":1"), std::string::npos);
    EXPECT_NE(first.find("\"id\":\"ui_motion_timeline_v1\""), std::string::npos);
    EXPECT_NE(first.find("\"timeline_capacity\":256"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_track_capacity\":1024"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_keyframe_capacity\":4096"), std::string::npos);
    EXPECT_NE(first.find("\"active_timeline_capacity\":256"), std::string::npos);
    EXPECT_NE(first.find("\"active_timeline_tracks\":0"), std::string::npos);
    EXPECT_NE(first.find("\"zero_active_iterations\":2"), std::string::npos);
    EXPECT_NE(first.find("\"sampled_tracks\":0"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_high_water\":256"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_track_high_water\":1024"), std::string::npos);
    EXPECT_NE(first.find("\"keyframe_high_water\":4096"), std::string::npos);
    EXPECT_NE(first.find("\"active_timeline_high_water\":0"), std::string::npos);
    EXPECT_NE(first.find("\"delta\":0"), std::string::npos);

    const std::string firstChecksum = extractChecksum(first);
    const std::string secondChecksum = extractChecksum(second);
    ASSERT_EQ(firstChecksum.size(), 16U);
    EXPECT_EQ(firstChecksum, secondChecksum);
}

TEST(UIBenchmarkWorkloadsTests, TimelineMotionActiveSeedsReportBoundedPaintOnlyCounters)
{
    // seed % 3 == 1 -> 64 active tracks = 16 timelines.
    const std::string first = runUIWorkload(TimelineMotionWorkload, 2, 1);
    const std::string second = runUIWorkload(TimelineMotionWorkload, 2, 1);

    EXPECT_NE(first.find("\"active_timeline_tracks\":64"), std::string::npos);
    EXPECT_NE(first.find("\"sampled_tracks\":128"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_high_water\":256"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_track_high_water\":1024"), std::string::npos);
    EXPECT_NE(first.find("\"keyframe_high_water\":4096"), std::string::npos);
    EXPECT_NE(first.find("\"active_timeline_high_water\":16"), std::string::npos);
    EXPECT_NE(first.find("\"paint_only_timeline_does_not_rebuild_layout_or_hit\""),
              std::string::npos);
    EXPECT_NE(first.find("\"paint_only_workload_excludes_layout_tracks\""),
              std::string::npos);
    EXPECT_NE(first.find("\"delta\":0"), std::string::npos);

    const std::string firstChecksum = extractChecksum(first);
    const std::string secondChecksum = extractChecksum(second);
    ASSERT_EQ(firstChecksum.size(), 16U);
    EXPECT_EQ(firstChecksum, secondChecksum);
}

TEST(UIBenchmarkWorkloadsTests, LayoutTimelineMotionZeroActiveKeepsCleanSnapshots)
{
    const std::string first = runUIWorkload(LayoutTimelineMotionWorkload, 2, 0);
    const std::string second = runUIWorkload(LayoutTimelineMotionWorkload, 2, 0);

    EXPECT_NE(first.find("\"status\":\"ok\",\"schema\":1"), std::string::npos);
    EXPECT_NE(first.find("\"id\":\"ui_motion_layout_v1\""), std::string::npos);
    EXPECT_NE(first.find("\"active_timeline_tracks\":0"), std::string::npos);
    EXPECT_NE(first.find("\"sampled_tracks\":0"), std::string::npos);
    EXPECT_NE(first.find("\"sampled_layout_tracks\":0"), std::string::npos);
    EXPECT_NE(first.find("\"layout_commit_failures\":0"), std::string::npos);
    EXPECT_NE(first.find("\"layout_passes\":0"), std::string::npos);
    EXPECT_NE(first.find("\"hit_rebuilds\":0"), std::string::npos);
    EXPECT_NE(first.find("\"paint_snapshot_rebuilds\":0"), std::string::npos);
    EXPECT_NE(first.find("\"zero_active_iterations\":2"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_high_water\":256"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_track_high_water\":1024"), std::string::npos);
    EXPECT_NE(first.find("\"keyframe_high_water\":4096"), std::string::npos);
    EXPECT_NE(first.find("\"active_timeline_high_water\":0"), std::string::npos);
    EXPECT_NE(first.find("\"delta\":0"), std::string::npos);

    const std::string firstChecksum = extractChecksum(first);
    const std::string secondChecksum = extractChecksum(second);
    ASSERT_EQ(firstChecksum.size(), 16U);
    EXPECT_EQ(firstChecksum, secondChecksum);
}

TEST(UIBenchmarkWorkloadsTests, LayoutTimelineMotionActiveSeedsReportAtomicRebuildCounters)
{
    const std::string first = runUIWorkload(LayoutTimelineMotionWorkload, 2, 1);
    const std::string second = runUIWorkload(LayoutTimelineMotionWorkload, 2, 1);

    EXPECT_NE(first.find("\"active_timeline_tracks\":64"), std::string::npos);
    EXPECT_NE(first.find("\"sampled_tracks\":128"), std::string::npos);
    EXPECT_NE(first.find("\"sampled_layout_tracks\":128"), std::string::npos);
    EXPECT_NE(first.find("\"layout_commit_failures\":0"), std::string::npos);
    EXPECT_NE(first.find("\"layout_passes\":2"), std::string::npos);
    EXPECT_NE(first.find("\"hit_rebuilds\":2"), std::string::npos);
    EXPECT_NE(first.find("\"paint_snapshot_rebuilds\":2"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_high_water\":256"), std::string::npos);
    EXPECT_NE(first.find("\"timeline_track_high_water\":1024"), std::string::npos);
    EXPECT_NE(first.find("\"keyframe_high_water\":4096"), std::string::npos);
    EXPECT_NE(first.find("\"active_timeline_high_water\":16"), std::string::npos);
    EXPECT_NE(first.find("\"layout_timeline_rebuilds_layout_hit_and_paint_atomically\""),
              std::string::npos);
    EXPECT_EQ(first.find("\"paint_only_timeline_does_not_rebuild_layout_or_hit\""),
              std::string::npos);
    EXPECT_EQ(first.find("\"paint_only_workload_excludes_layout_tracks\""),
              std::string::npos);
    EXPECT_NE(first.find("\"delta\":0"), std::string::npos);

    const std::string firstChecksum = extractChecksum(first);
    const std::string secondChecksum = extractChecksum(second);
    ASSERT_EQ(firstChecksum.size(), 16U);
    EXPECT_EQ(firstChecksum, secondChecksum);
}

TEST(UIBenchmarkWorkloadsTests, TimelineMotionCapacitySeedExercisesEveryDefinitionSlot)
{
    // seed % 3 == 2 -> 256 timelines, 1024 tracks, and 4096 keyframes.
    const std::string paintOnly = runUIWorkload(TimelineMotionWorkload, 1, 2);
    const std::string layout = runUIWorkload(LayoutTimelineMotionWorkload, 1, 2);

    const std::array outputs{&paintOnly, &layout};
    for (const std::string* output : outputs) {
        EXPECT_NE(output->find("\"active_timeline_tracks\":1024"), std::string::npos);
        EXPECT_NE(output->find("\"sampled_timelines\":256"), std::string::npos);
        EXPECT_NE(output->find("\"sampled_tracks\":1024"), std::string::npos);
        EXPECT_NE(output->find("\"sampled_segments\":1024"), std::string::npos);
        EXPECT_NE(output->find("\"active_timelines_sum\":256"), std::string::npos);
        EXPECT_NE(output->find("\"timeline_high_water\":256"), std::string::npos);
        EXPECT_NE(output->find("\"timeline_track_high_water\":1024"),
                  std::string::npos);
        EXPECT_NE(output->find("\"keyframe_high_water\":4096"), std::string::npos);
        EXPECT_NE(output->find("\"active_timeline_high_water\":256"),
                  std::string::npos);
        EXPECT_NE(output->find("\"delta\":0"), std::string::npos);
    }
    EXPECT_NE(layout.find("\"sampled_layout_tracks\":1024"), std::string::npos);
    EXPECT_NE(layout.find("\"layout_passes\":1"), std::string::npos);
    EXPECT_NE(layout.find("\"hit_rebuilds\":1"), std::string::npos);
    EXPECT_NE(layout.find("\"paint_snapshot_rebuilds\":1"), std::string::npos);
}

} // namespace
} // namespace Tina::Bench
