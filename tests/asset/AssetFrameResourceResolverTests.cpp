#include <tina/asset/AssetFrameResourceResolver.hpp>
#include <tina/render/RenderFramePacket.hpp>

#include <gtest/gtest.h>

namespace Tina::Asset {
namespace {

TEST(AssetFrameResourceResolverTests, MissingCallbackReturnsEmptyWithoutTouchingSink)
{
    const AssetFrameResourceResolver resolver{};
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());

    const auto resource = resolver({}, packet.resourceSink());

    ASSERT_TRUE(resource.has_value()) << resource.error().message;
    EXPECT_FALSE(static_cast<bool>(*resource));
    EXPECT_EQ(packet.resourceCount(), 0U);
}

} // namespace
} // namespace Tina::Asset
