#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::UI {

// Fixed side-state slots reserved before a component mutates the retained tree.
struct UIBehaviorSlotBudget final {
    usize activate = 0;
    usize toggle = 0;
    usize range = 0;
    usize textInput = 0;
    usize scroll = 0;
    usize selection = 0;

    bool operator==(const UIBehaviorSlotBudget&) const = default;
};

// Complete retained-storage reservation for one bounded component build.
struct UIComponentBuildBudget final {
    usize nodes = 0;
    usize textBytes = 0;
    usize canvasCommands = 0;
    UIBehaviorSlotBudget behaviors{};

    bool operator==(const UIComponentBuildBudget&) const = default;
};

struct UIComponentBuildPoolStatistics final {
    usize requested = 0;
    usize reserved = 0;
    usize published = 0;
    usize capacityFailures = 0;
    usize outstandingReservations = 0;
};

struct UIComponentBehaviorBuildStatistics final {
    UIComponentBuildPoolStatistics activate{};
    UIComponentBuildPoolStatistics toggle{};
    UIComponentBuildPoolStatistics range{};
    UIComponentBuildPoolStatistics textInput{};
    UIComponentBuildPoolStatistics scroll{};
    UIComponentBuildPoolStatistics selection{};
};

struct UIComponentBuildStatistics final {
    UIComponentBuildPoolStatistics nodes{};
    UIComponentBuildPoolStatistics textBytes{};
    UIComponentBuildPoolStatistics canvasCommands{};
    UIComponentBehaviorBuildStatistics behaviors{};
    usize activeTransactionCount = 0;
    usize transactionFailureCount = 0;
};

} // namespace Tina::UI
