#include "TestHarness.hpp"

#include <cstdio>

namespace Tina::Tests {

void runAssertTests();
void runEnumFlagsTests();
void runLegacyCompatibilityTests();
void runResultTests();
void runScopeExitTests();
void runTimeTests();
void runTypesTests();

} // namespace Tina::Tests

int main()
{
    using namespace Tina::Tests;

    runTypesTests();
    runEnumFlagsTests();
    runResultTests();
    runAssertTests();
    runTimeTests();
    runScopeExitTests();
    runLegacyCompatibilityTests();

    if (FailureCount != 0) {
        std::fprintf(stderr, "Tina Core tests failed: %d check(s)\n", FailureCount);
        return 1;
    }

    std::puts("Tina Core tests passed");
    return 0;
}
