#include <tina/core/BuildInfo.hpp>

#include <iostream>
#include <string_view>

int main()
{
    const auto& build = Tina::Core::buildInfo();
    if (build.version != TINA_EXPECTED_SDK_VERSION ||
        build.buildId != TINA_EXPECTED_SDK_BUILD_ID || build.buildId.size() != 64)
    {
        std::cerr << "SDK archive and package metadata do not match\n";
        return 1;
    }
    std::cout << "{\"version\":\"" << build.version
              << "\",\"buildId\":\"" << build.buildId
              << "\",\"configuration\":\"" << build.configuration
              << "\",\"features\":\"" << build.features << "\"}\n";
    return 0;
}
