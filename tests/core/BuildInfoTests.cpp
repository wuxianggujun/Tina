#include <tina/core/BuildInfo.hpp>

#include <gtest/gtest.h>

#include <algorithm>

TEST(BuildInfoTest, IdentifiesTheLinkedArchiveWithoutAnAllocationOrMutableOwner)
{
    const auto& information = Tina::Core::buildInfo();
    EXPECT_EQ(&information, &Tina::Core::buildInfo());
    EXPECT_FALSE(information.version.empty());
    EXPECT_FALSE(information.configuration.empty());
    EXPECT_FALSE(information.compiler.empty());
    EXPECT_FALSE(information.compilerVersion.empty());
    EXPECT_FALSE(information.platform.empty());
    EXPECT_TRUE(information.features.starts_with("GameSDK"));
    ASSERT_EQ(information.buildId.size(), 64U);
    EXPECT_TRUE(std::ranges::all_of(information.buildId, [](char digit) {
        return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
    }));
}
