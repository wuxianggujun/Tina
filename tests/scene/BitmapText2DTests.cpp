#include <tina/scene/BitmapText2D.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include "support/BitmapFontTestSupport.hpp"
#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace Tina::Tests {
namespace {
using namespace BitmapFontFixture;

struct PageResolver final {
    Core::u32 releases = 0;
    Core::u32 calls = 0;
    Core::u32 extent = 16;
    bool ready = true;
    Render::Texture2DFrameResourceResolver view() noexcept { return {this, &resolve}; }
    static Core::Result<std::optional<Render::Texture2DFrameResourceResolution>> resolve(
        void* owner, Core::AssetId id, Render::FrameResourceSink& sink) noexcept {
        auto& self = *static_cast<PageResolver*>(owner);
        ++self.calls;
        if (!self.ready) return std::optional<Render::Texture2DFrameResourceResolution>{};
        const auto key = std::to_integer<Core::u64>(id.bytes()[0]);
        auto resource = sink.intern({.kind = Render::FrameResourceKind::Texture2D, .deviceBindingKey = key},
            Render::FramePin{Render::FramePinKind::Custom, key, &self, [](void* value) noexcept {
                ++static_cast<PageResolver*>(value)->releases;
            }});
        if (!resource) return Core::failure(resource.error());
        return std::optional<Render::Texture2DFrameResourceResolution>{{*resource, self.extent, self.extent}};
    }
};

Scene::BitmapText2D label()
{
    return Scene::BitmapText2D::Create(std::make_shared<const Text::BitmapFont>(font()),
        std::array{assetId(1), assetId(2)}).value();
}

Render::RenderCamera2DInput camera()
{
    return {.stableCameraKey = 1, .worldWidth = 100, .worldHeight = 100, .actualPixelsPerMeter = 10};
}

TEST(BitmapText2DTests, EmitsOnlyInkQuadsAndPinsPagesWithoutPerGlyphEntities)
{
    auto text = label();
    ASSERT_TRUE(text.setText("A V"));
    PageResolver resolver;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    auto builder = Render::RenderSceneBuilder::Create({.spriteCapacity = 8}).value();
    ASSERT_TRUE(builder.beginFrame());
    auto writer = builder.writer();
    ASSERT_TRUE(writer.setCamera2D(camera()));
    const Core::ColorTransform color{{0.5F, 1, 2, 0.75F}, {0.1F, 0.2F, 0.3F, 0.1F}};
    ASSERT_TRUE(text.extract(writer, packet.resourceSink(), resolver.view(),
        {.x = 1, .y = 2, .pixelsPerMeter = 10, .firstStableKey = 100, .colorTransform = color}));
    auto scene = builder.commit();
    ASSERT_TRUE(scene);
    ASSERT_EQ(scene->sprites2D().size(), 2U);
    const auto& coverage = scene->sprites2D()[0];
    const auto& colored = scene->sprites2D()[1];
    EXPECT_EQ(coverage.stableEntityKey, 100U);
    EXPECT_EQ(colored.stableEntityKey, 102U); // the space retains its layout identity
    EXPECT_FLOAT_EQ(coverage.quad.centerX, 1.2F);
    EXPECT_FLOAT_EQ(coverage.quad.centerY, 1.5F);
    EXPECT_FLOAT_EQ(coverage.u0, 0.25F);
    EXPECT_FLOAT_EQ(coverage.colorTransform.multiply.red, 0);
    EXPECT_FLOAT_EQ(coverage.colorTransform.add.red, 0.6F);
    EXPECT_EQ(colored.colorTransform, color);
    EXPECT_NE(coverage.texture, colored.texture);
    EXPECT_EQ(resolver.calls, 2U);
    EXPECT_EQ(resolver.releases, 0U);
    ASSERT_TRUE(packet.abandon());
    EXPECT_EQ(resolver.releases, 2U);
}

TEST(BitmapText2DTests, IsometricTextSharesAnchorDepthAndDoesNotProjectGlyphOffsetsTwice)
{
    auto text = label();
    ASSERT_TRUE(text.setText("AV", {.scale = 2}));
    PageResolver resolver;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    auto builder = Render::RenderSceneBuilder::Create({.spriteCapacity = 8}).value();
    ASSERT_TRUE(builder.beginFrame());
    auto writer = builder.writer();
    auto view = camera();
    view.isometricProjection = Render::IsometricProjection2D{.tileWidthMeters = 2, .tileHeightMeters = 1, .elevationStepMeters = 0.5F};
    ASSERT_TRUE(writer.setCamera2D(view));
    ASSERT_TRUE(text.extract(writer, packet.resourceSink(), resolver.view(), {.x = 3, .y = 1, .elevation = 2, .pixelsPerMeter = 10}));
    auto scene = builder.commit();
    ASSERT_TRUE(scene);
    ASSERT_EQ(scene->sprites2D().size(), 2U);
    EXPECT_DOUBLE_EQ(scene->sprites2D()[0].sortDepth, -1);
    EXPECT_DOUBLE_EQ(scene->sprites2D()[1].sortDepth, -1);
    EXPECT_FLOAT_EQ(scene->sprites2D()[0].quad.centerX, 2.4F);
    EXPECT_FLOAT_EQ(scene->sprites2D()[0].quad.halfAxisXX, 0.4F);
    EXPECT_FLOAT_EQ(scene->sprites2D()[1].quad.centerX - scene->sprites2D()[0].quad.centerX, 0.8F);
}

TEST(BitmapText2DTests, FailedTextMutationKeepsThePreviousLayoutAndDrawFailuresStayExplicit)
{
    auto text = label();
    ASSERT_TRUE(text.setText("AV"));
    const auto width = text.layout().width;
    EXPECT_FALSE(text.setText(std::string_view{"A\0B", 3}));
    EXPECT_FLOAT_EQ(text.layout().width, width);
    EXPECT_EQ(text.layout().glyphs.size(), 2U);
    EXPECT_FALSE(Scene::BitmapText2D::Create(nullptr, {}));
    EXPECT_FALSE(Scene::BitmapText2D::Create(std::make_shared<const Text::BitmapFont>(font()), {}));
    PageResolver resolver;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    auto builder = Render::RenderSceneBuilder::Create({.spriteCapacity = 8}).value();
    ASSERT_TRUE(builder.beginFrame());
    auto writer = builder.writer();
    ASSERT_TRUE(writer.setCamera2D(camera()));
    EXPECT_FALSE(text.extract(writer, packet.resourceSink(), {}, {}));
    resolver.extent = 8;
    EXPECT_FALSE(text.extract(writer, packet.resourceSink(), resolver.view(), {}));
    resolver.extent = 16;
    EXPECT_FALSE(text.extract(writer, packet.resourceSink(), resolver.view(),
        {.firstStableKey = (std::numeric_limits<Core::u64>::max)()}));
    ASSERT_TRUE(text.setText("A"));
    EXPECT_TRUE(text.extract(writer, packet.resourceSink(), resolver.view(),
        {.firstStableKey = (std::numeric_limits<Core::u64>::max)()}));
}

} // namespace
} // namespace Tina::Tests
