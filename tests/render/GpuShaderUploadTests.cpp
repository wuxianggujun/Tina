#include <gtest/gtest.h>

#include <tina/render/FramePin.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace Tina::Tests {
namespace {

void countPinRelease(void* userData) noexcept
{
    ++*static_cast<Core::u32*>(userData);
}

// Stand-in for a cooked binary. Contents are never inspected by the device: only a real backend
// hands them to a shader compiler, so what a headless test can pin is the table shape.
constexpr std::array<std::byte, 4> kBinaryBytes{std::byte{'F'}, std::byte{'S'}, std::byte{0x01},
                                               std::byte{0x02}};

[[nodiscard]] Core::Result<Render::GpuShaderId>
uploadFragment(Render::IRenderDevice& device,
               Render::GpuShaderKind kind = Render::GpuShaderKind::Sprite2D)
{
    const std::array binaries{
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::Glsl120,
                                .bytes = kBinaryBytes},
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::SpirV,
                                .bytes = kBinaryBytes},
    };
    return device.createShader(Render::GpuShaderUploadDesc{.shaderKind = kind,
                                                           .binaries = binaries});
}

[[nodiscard]] Render::GpuShaderUniformValue namedValue(std::string_view name,
                                                      std::array<float, 4> value) noexcept
{
    Render::GpuShaderUniformValue entry{};
    entry.value = value;
    const Core::usize length =
        (std::min)(name.size(), static_cast<Core::usize>(Render::GpuShaderUniformValue::MaximumNameBytes));
    std::copy_n(name.begin(), length, entry.name.begin());
    return entry;
}

[[nodiscard]] Render::GpuShaderTextureValue namedTexture(std::string_view name,
                                                        Render::GpuTextureId texture) noexcept
{
    Render::GpuShaderTextureValue entry{};
    entry.texture = texture;
    const Core::usize length =
        (std::min)(name.size(), static_cast<Core::usize>(Render::GpuShaderTextureValue::MaximumNameBytes));
    std::copy_n(name.begin(), length, entry.name.begin());
    return entry;
}

constexpr std::array<std::byte, 4> kWhitePixel{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                               std::byte{0xFF}};

[[nodiscard]] Core::Result<Render::GpuTextureId> uploadTexture(Render::IRenderDevice& device)
{
    const std::array levels{
        Render::Texture2DUploadLevel{.width = 1, .height = 1, .bytes = kWhitePixel}};
    return device.createTexture2D(Render::Texture2DUploadDesc{.levels = levels});
}

} // namespace

TEST(ShaderUploadDescTest, AcceptsEveryProfileInAscendingOrder)
{
    const std::array binaries{
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::Glsl120,
                                .bytes = kBinaryBytes},
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::SpirV,
                                .bytes = kBinaryBytes},
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::Dxbc50,
                                .bytes = kBinaryBytes},
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::Essl300,
                                .bytes = kBinaryBytes},
    };
    EXPECT_TRUE(Render::validateShaderUploadDesc(
                    Render::GpuShaderUploadDesc{.shaderKind = Render::GpuShaderKind::Sprite2D,
                                                .binaries = binaries})
                    .has_value());
}

// Mesh3D is a full path now: both 3D draw item kinds carry a shader ref and bgfx links one cooked
// fragment binary against the rigid and skinned engine vertex stages. The shared validator must
// accept it, because a kind refused here but linked by the backend is unreachable capability, and a
// kind accepted here but unlinked by the backend is headless-green/real-backend-red.
TEST(ShaderUploadDescTest, AcceptsMesh3DBecauseBothDrawPathsCanBindIt)
{
    const std::array binaries{
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::Glsl120,
                                .bytes = kBinaryBytes},
    };
    EXPECT_TRUE(Render::validateShaderUploadDesc(
                    Render::GpuShaderUploadDesc{.shaderKind = Render::GpuShaderKind::Mesh3D,
                                                .binaries = binaries})
                    .has_value());

    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto uploaded = uploadFragment(**device, Render::GpuShaderKind::Mesh3D);
    ASSERT_TRUE(uploaded.has_value()) << uploaded.error().message;
    EXPECT_TRUE((*device)->validateShader(*uploaded).has_value());
}

TEST(ShaderUploadDescTest, RejectsInvalidKindProfileAndBinaryTable)
{
    const std::array oneBinary{
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::Glsl120,
                                .bytes = kBinaryBytes},
    };
    auto invalidKind = Render::validateShaderUploadDesc(
        Render::GpuShaderUploadDesc{.shaderKind = Render::GpuShaderKind::Invalid,
                                    .binaries = oneBinary});
    ASSERT_FALSE(invalidKind.has_value());
    EXPECT_EQ(invalidKind.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    auto emptyTable = Render::validateShaderUploadDesc(
        Render::GpuShaderUploadDesc{.shaderKind = Render::GpuShaderKind::Sprite2D});
    ASSERT_FALSE(emptyTable.has_value());
    EXPECT_EQ(emptyTable.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    const std::array invalidProfile{
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::Invalid,
                                .bytes = kBinaryBytes},
    };
    auto badProfile = Render::validateShaderUploadDesc(
        Render::GpuShaderUploadDesc{.shaderKind = Render::GpuShaderKind::Sprite2D,
                                    .binaries = invalidProfile});
    ASSERT_FALSE(badProfile.has_value());
    EXPECT_EQ(badProfile.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    // Duplicates and descending order are the same defect: the backend would have two candidates
    // for one renderer type and no rule for picking between them.
    const std::array duplicated{
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::SpirV,
                                .bytes = kBinaryBytes},
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::SpirV,
                                .bytes = kBinaryBytes},
    };
    auto duplicate = Render::validateShaderUploadDesc(
        Render::GpuShaderUploadDesc{.shaderKind = Render::GpuShaderKind::Sprite2D,
                                    .binaries = duplicated});
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    const std::array descending{
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::SpirV,
                                .bytes = kBinaryBytes},
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::Glsl120,
                                .bytes = kBinaryBytes},
    };
    auto unsorted = Render::validateShaderUploadDesc(
        Render::GpuShaderUploadDesc{.shaderKind = Render::GpuShaderKind::Sprite2D,
                                    .binaries = descending});
    ASSERT_FALSE(unsorted.has_value());
    EXPECT_EQ(unsorted.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    // An empty binary would reach the backend's shader compiler as a zero-length blob, which is a
    // cook defect the device must not launder into a successful upload.
    const std::array emptyBinary{
        Render::GpuShaderBinary{.profile = Render::GpuShaderBinaryProfile::Glsl120},
    };
    auto empty = Render::validateShaderUploadDesc(
        Render::GpuShaderUploadDesc{.shaderKind = Render::GpuShaderKind::Sprite2D,
                                    .binaries = emptyBinary});
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

TEST(ShaderUploadDescTest, RejectsMoreBinariesThanTheMaximum)
{
    // One past MaximumBinaryCount cannot be built from distinct profiles, since only four exist.
    // The count check must therefore fire before the ordering check, or an over-long table would be
    // reported as an ordering defect.
    std::array<Render::GpuShaderBinary, Render::GpuShaderUploadDesc::MaximumBinaryCount + 1U> many{};
    for (auto& binary : many)
    {
        binary.profile = Render::GpuShaderBinaryProfile::Glsl120;
        binary.bytes = kBinaryBytes;
    }
    auto tooMany = Render::validateShaderUploadDesc(
        Render::GpuShaderUploadDesc{.shaderKind = Render::GpuShaderKind::Sprite2D,
                                    .binaries = many});
    ASSERT_FALSE(tooMany.has_value());
    EXPECT_EQ(tooMany.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

TEST(NullRenderDeviceShaderTest, CreateBindDestroyLifecycle)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    auto foreignDevice = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(foreignDevice.has_value());

    auto shader = uploadFragment(**device);
    ASSERT_TRUE(shader.has_value()) << shader.error().message;
    auto foreignShader = uploadFragment(**foreignDevice);
    ASSERT_TRUE(foreignShader.has_value()) << foreignShader.error().message;
    // Same slot, different owner: the owner field is what rejects cross-device use, since two
    // independent devices hand out identical index/generation pairs.
    EXPECT_EQ(shader->index, foreignShader->index);
    EXPECT_EQ(shader->generation, foreignShader->generation);
    EXPECT_NE(shader->owner, foreignShader->owner);
    EXPECT_EQ((*device)->statistics().liveResources, 1U);

    EXPECT_TRUE((*device)->validateShader(*shader).has_value());
    auto foreignValidation = (*device)->validateShader(*foreignShader);
    ASSERT_FALSE(foreignValidation.has_value());
    EXPECT_EQ(foreignValidation.error().code, Render::RenderErrorCode::ShaderNotFound);
    auto foreignBinding = (*device)->setShaderBinding(1U, *foreignShader);
    ASSERT_FALSE(foreignBinding.has_value());
    EXPECT_EQ(foreignBinding.error().code, Render::RenderErrorCode::ShaderNotFound);
    auto foreignDestroy = (*device)->destroyShader(*foreignShader);
    ASSERT_FALSE(foreignDestroy.has_value());
    EXPECT_EQ(foreignDestroy.error().code, Render::RenderErrorCode::ShaderNotFound);

    ASSERT_TRUE((*device)->setShaderBinding(1U, *shader).has_value());
    ASSERT_TRUE((*device)->setShaderBinding(1U, {}).has_value());

    // Key 0 is not assignable: an unbound descriptor reads as key 0, so accepting it would let a
    // draw that never bound a shader resolve one.
    auto zeroKey = (*device)->setShaderBinding(0U, *shader);
    ASSERT_FALSE(zeroKey.has_value());
    EXPECT_EQ(zeroKey.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    ASSERT_TRUE((*device)->destroyShader(*shader).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 0U);
    auto staleValidation = (*device)->validateShader(*shader);
    ASSERT_FALSE(staleValidation.has_value());
    EXPECT_EQ(staleValidation.error().code, Render::RenderErrorCode::ShaderNotFound);
    auto doubleDestroy = (*device)->destroyShader(*shader);
    ASSERT_FALSE(doubleDestroy.has_value());
    EXPECT_EQ(doubleDestroy.error().code, Render::RenderErrorCode::ShaderNotFound);
}

TEST(NullRenderDeviceShaderTest, DestroyClearsEveryBindingThatNamedTheShader)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto shader = uploadFragment(**device);
    ASSERT_TRUE(shader.has_value());
    // A second distinct shader, not a second kind: what this pins is that destroy drops the bindings
    // that named the destroyed shader and leaves the survivor's alone.
    auto other = uploadFragment(**device);
    ASSERT_TRUE(other.has_value());

    ASSERT_TRUE((*device)->setShaderBinding(1U, *shader).has_value());
    ASSERT_TRUE((*device)->setShaderBinding(2U, *shader).has_value());
    ASSERT_TRUE((*device)->setShaderBinding(3U, *other).has_value());
    ASSERT_TRUE((*device)->destroyShader(*shader).has_value());

    // Rebinding the surviving shader onto the freed keys must succeed, which it only can if destroy
    // actually dropped them rather than leaving a stale entry a later frame could resolve.
    EXPECT_TRUE((*device)->setShaderBinding(1U, *other).has_value());
    EXPECT_TRUE((*device)->setShaderBinding(2U, *other).has_value());
    EXPECT_TRUE((*device)->validateShader(*other).has_value());
}

TEST(NullRenderDeviceShaderTest, GenerationRejectsAStaleHandleAfterSlotReuse)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto first = uploadFragment(**device);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE((*device)->destroyShader(*first).has_value());

    auto second = uploadFragment(**device);
    ASSERT_TRUE(second.has_value());
    // A stale handle must not resolve even when a later upload occupies the same logical slot.
    auto stale = (*device)->validateShader(*first);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, Render::RenderErrorCode::ShaderNotFound);
    auto staleBinding = (*device)->setShaderBinding(1U, *first);
    ASSERT_FALSE(staleBinding.has_value());
    EXPECT_EQ(staleBinding.error().code, Render::RenderErrorCode::ShaderNotFound);
}

TEST(NullRenderDeviceShaderTest, RetirementPinCompletesImmediatelyAndIsNotConsumedOnFailure)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto shader = uploadFragment(**device);
    ASSERT_TRUE(shader.has_value());

    Core::u32 releases = 0;
    Render::FramePin completionPin{Render::FramePinKind::AssetLease, 11, &releases, &countPinRelease};
    ASSERT_TRUE((*device)->retireShader(*shader, completionPin).has_value());
    EXPECT_FALSE(completionPin.hasValue());
    EXPECT_EQ(releases, 1U);
    EXPECT_EQ((*device)->statistics().completedGpuRetirements, 1U);

    // A failed retire must leave the pin held: releasing it would tell the caller the GPU is done
    // with a resource the device never touched.
    Render::FramePin failurePin{Render::FramePinKind::AssetLease, 12, &releases, &countPinRelease};
    auto stale = (*device)->retireShader(*shader, failurePin);
    ASSERT_FALSE(stale.has_value());
    EXPECT_TRUE(failurePin.hasValue());
    EXPECT_EQ(releases, 1U);
    failurePin.release();
    EXPECT_EQ(releases, 2U);
}

TEST(NullRenderDeviceShaderTest, ShutdownRejectsEverySubsequentShaderCall)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto shader = uploadFragment(**device);
    ASSERT_TRUE(shader.has_value());
    (*device)->shutdown();

    auto created = uploadFragment(**device);
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code, Render::RenderErrorCode::DeviceStopped);
    auto validated = (*device)->validateShader(*shader);
    ASSERT_FALSE(validated.has_value());
    EXPECT_EQ(validated.error().code, Render::RenderErrorCode::DeviceStopped);
    auto bound = (*device)->setShaderBinding(1U, *shader);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error().code, Render::RenderErrorCode::DeviceStopped);
    auto destroyed = (*device)->destroyShader(*shader);
    ASSERT_FALSE(destroyed.has_value());
    EXPECT_EQ(destroyed.error().code, Render::RenderErrorCode::DeviceStopped);
    auto valued = (*device)->setShaderUniformBinding(1U, Render::GpuShaderUniformBindingDesc{});
    ASSERT_FALSE(valued.has_value());
    EXPECT_EQ(valued.error().code, Render::RenderErrorCode::DeviceStopped);
}

TEST(ShaderUniformBindingDescTest, AcceptsDistinctFiniteNamedValues)
{
    const std::array values{namedValue("u_tint", {1.0F, 0.5F, 0.25F, 1.0F}),
                            namedValue("u_wobble", {2.0F, 0.0F, 0.0F, 0.0F})};
    EXPECT_TRUE(
        Render::validateShaderUniformBindingDesc(Render::GpuShaderUniformBindingDesc{.values = values})
            .has_value());
}

TEST(ShaderUniformBindingDescTest, RejectsEmptyNameNonFiniteValueAndDuplicateName)
{
    const std::array empty{namedValue("", {0.0F, 0.0F, 0.0F, 0.0F})};
    auto emptyName =
        Render::validateShaderUniformBindingDesc(Render::GpuShaderUniformBindingDesc{.values = empty});
    ASSERT_FALSE(emptyName.has_value());
    EXPECT_EQ(emptyName.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    const std::array infinite{
        namedValue("u_tint", {std::numeric_limits<float>::infinity(), 0.0F, 0.0F, 0.0F})};
    auto nonFinite =
        Render::validateShaderUniformBindingDesc(Render::GpuShaderUniformBindingDesc{.values = infinite});
    ASSERT_FALSE(nonFinite.has_value());
    EXPECT_EQ(nonFinite.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    const std::array duplicate{namedValue("u_tint", {1.0F, 0.0F, 0.0F, 0.0F}),
                               namedValue("u_tint", {0.0F, 1.0F, 0.0F, 0.0F})};
    auto duplicated = Render::validateShaderUniformBindingDesc(
        Render::GpuShaderUniformBindingDesc{.values = duplicate});
    ASSERT_FALSE(duplicated.has_value());
    EXPECT_EQ(duplicated.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

// An entry filling every byte has no terminator, so reading its name would run past the array. The
// validator must treat that as an empty name rather than trusting the buffer.
TEST(ShaderUniformBindingDescTest, RejectsAnUnterminatedName)
{
    Render::GpuShaderUniformValue unterminated{};
    unterminated.name.fill('u');
    const std::array values{unterminated};
    auto status =
        Render::validateShaderUniformBindingDesc(Render::GpuShaderUniformBindingDesc{.values = values});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

TEST(ShaderUniformBindingDescTest, RejectsMoreValuesThanTheMaximum)
{
    std::array<Render::GpuShaderUniformValue, Render::GpuShaderUniformBindingDesc::MaximumValueCount + 1>
        values{};
    for (Core::usize index = 0; index < values.size(); ++index)
    {
        values[index] = namedValue("u_" + std::to_string(index), {0.0F, 0.0F, 0.0F, 0.0F});
    }
    auto status =
        Render::validateShaderUniformBindingDesc(Render::GpuShaderUniformBindingDesc{.values = values});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

TEST(NullRenderDeviceShaderTest, UniformBindingRoundTripsAndClearsOnAnEmptyTable)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    const std::array values{namedValue("u_tint", {1.0F, 0.0F, 0.0F, 1.0F})};
    EXPECT_TRUE(
        (*device)->setShaderUniformBinding(7U, Render::GpuShaderUniformBindingDesc{.values = values})
            .has_value());
    // Clearing is not an error: it is how a caller drops a material without destroying its shader.
    EXPECT_TRUE((*device)->setShaderUniformBinding(7U, Render::GpuShaderUniformBindingDesc{}).has_value());
    EXPECT_TRUE((*device)->setShaderUniformBinding(9U, Render::GpuShaderUniformBindingDesc{}).has_value());

    auto zeroKey =
        (*device)->setShaderUniformBinding(0U, Render::GpuShaderUniformBindingDesc{.values = values});
    ASSERT_FALSE(zeroKey.has_value());
    EXPECT_EQ(zeroKey.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

TEST(ShaderTextureBindingDescTest, RejectsEmptyNameInvalidIdAndDuplicateName)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto texture = uploadTexture(**device);
    ASSERT_TRUE(texture.has_value());

    const std::array unnamed{namedTexture("", *texture)};
    EXPECT_FALSE(Render::validateShaderTextureBindingDesc(
                     Render::GpuShaderTextureBindingDesc{.values = unnamed})
                     .has_value());

    const std::array unset{namedTexture("s_mask", Render::GpuTextureId{})};
    EXPECT_FALSE(
        Render::validateShaderTextureBindingDesc(Render::GpuShaderTextureBindingDesc{.values = unset})
            .has_value());

    // Two entries for one sampler means the caller believes something the device cannot honour, so it
    // is rejected rather than resolved last-wins.
    const std::array duplicate{namedTexture("s_mask", *texture), namedTexture("s_mask", *texture)};
    EXPECT_FALSE(Render::validateShaderTextureBindingDesc(
                     Render::GpuShaderTextureBindingDesc{.values = duplicate})
                     .has_value());

    // A name that fills every byte without a terminator is unterminated, not a 64-byte name.
    Render::GpuShaderTextureValue unterminated{};
    unterminated.texture = *texture;
    unterminated.name.fill('s');
    const std::array run{unterminated};
    EXPECT_FALSE(
        Render::validateShaderTextureBindingDesc(Render::GpuShaderTextureBindingDesc{.values = run})
            .has_value());
}

TEST(NullRenderDeviceShaderTest, TextureBindingRequiresALiveTextureOfThisDevice)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    auto foreignDevice = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(foreignDevice.has_value());

    auto texture = uploadTexture(**device);
    auto foreignTexture = uploadTexture(**foreignDevice);
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(foreignTexture.has_value());

    const std::array good{namedTexture("s_mask", *texture)};
    EXPECT_TRUE(
        (*device)->setShaderTextureBinding(4U, Render::GpuShaderTextureBindingDesc{.values = good})
            .has_value());

    // Shape-valid but owned elsewhere. Caught at bind time because a submit has no channel to report
    // it and would otherwise silently sample a default texture.
    const std::array foreign{namedTexture("s_mask", *foreignTexture)};
    auto crossDevice =
        (*device)->setShaderTextureBinding(5U, Render::GpuShaderTextureBindingDesc{.values = foreign});
    ASSERT_FALSE(crossDevice.has_value());
    EXPECT_EQ(crossDevice.error().code, Render::RenderErrorCode::TextureNotFound);

    ASSERT_TRUE((*device)->destroyTexture2D(*texture).has_value());
    auto retired =
        (*device)->setShaderTextureBinding(6U, Render::GpuShaderTextureBindingDesc{.values = good});
    ASSERT_FALSE(retired.has_value());
    EXPECT_EQ(retired.error().code, Render::RenderErrorCode::TextureNotFound);
}

TEST(NullRenderDeviceShaderTest, ValueAndTextureHalvesOfOneKeyClearIndependently)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto texture = uploadTexture(**device);
    ASSERT_TRUE(texture.has_value());

    const std::array values{namedValue("u_tint", {1.0F, 0.0F, 0.0F, 1.0F})};
    const std::array textures{namedTexture("s_mask", *texture)};
    ASSERT_TRUE(
        (*device)->setShaderUniformBinding(3U, Render::GpuShaderUniformBindingDesc{.values = values})
            .has_value());
    ASSERT_TRUE(
        (*device)->setShaderTextureBinding(3U, Render::GpuShaderTextureBindingDesc{.values = textures})
            .has_value());

    // Clearing one half must not disturb the other. The two halves share a key precisely so a material
    // is one key, which makes "clear" ambiguous unless each call owns only its own half -- and the
    // failure this pins is silent: a dropped texture falls back to a default rather than erroring.
    EXPECT_TRUE((*device)->setShaderUniformBinding(3U, Render::GpuShaderUniformBindingDesc{}).has_value());
    EXPECT_TRUE(
        (*device)->setShaderTextureBinding(3U, Render::GpuShaderTextureBindingDesc{.values = textures})
            .has_value());
    EXPECT_TRUE((*device)->setShaderTextureBinding(3U, Render::GpuShaderTextureBindingDesc{}).has_value());

    // Clearing a key that was never bound is not an error, matching the value half.
    EXPECT_TRUE((*device)->setShaderTextureBinding(99U, Render::GpuShaderTextureBindingDesc{}).has_value());

    auto zeroKey =
        (*device)->setShaderTextureBinding(0U, Render::GpuShaderTextureBindingDesc{.values = textures});
    ASSERT_FALSE(zeroKey.has_value());
    EXPECT_EQ(zeroKey.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

// Name of the declaration at `index`, as a view over the fixed-size array.
[[nodiscard]] std::string_view
declaredName(const Render::GpuShaderSamplerDeclarationTable& table, Core::usize index)
{
    return std::string_view{table.declarations[index].name.data()};
}

TEST(ShaderSamplerParseTest, ReadsNameAndRegisterInDeclarationOrder)
{
    constexpr std::string_view source = R"(SAMPLER2D(s_tex, 0);
SAMPLER2DSHADOW( s_shadow , 3 );
SAMPLERCUBE(s_env,4);
)";
    auto table = Render::parseShaderSamplerDeclarations(source);
    ASSERT_TRUE(table.has_value());
    ASSERT_EQ(table->count, 3U);
    EXPECT_EQ(declaredName(*table, 0), "s_tex");
    EXPECT_EQ(table->declarations[0].stage, 0U);
    // Whitespace around either argument is the author's business, not a different declaration.
    EXPECT_EQ(declaredName(*table, 1), "s_shadow");
    EXPECT_EQ(table->declarations[1].stage, 3U);
    EXPECT_EQ(declaredName(*table, 2), "s_env");
    EXPECT_EQ(table->declarations[2].stage, 4U);
}

TEST(ShaderSamplerParseTest, IgnoresCommentsPreprocessorLinesAndLongerIdentifiers)
{
    constexpr std::string_view source = R"(// SAMPLER2D(s_commented, 9);
/* SAMPLER2D(s_blocked, 8);
   still inside the block */
#define MY_MASK SAMPLER2D(s_defined, 7)
#include <tina_sprite2d.sh>
int MY_SAMPLER2D = 0;
SAMPLER2D(s_real, 2);
)";
    auto table = Render::parseShaderSamplerDeclarations(source);
    ASSERT_TRUE(table.has_value());
    // Only the one real declaration: an include is not expanded (the engine set is known from
    // GpuShaderTextureStages) and a macro body declares nothing where it is defined.
    ASSERT_EQ(table->count, 1U);
    EXPECT_EQ(declaredName(*table, 0), "s_real");
    EXPECT_EQ(table->declarations[0].stage, 2U);
}

TEST(ShaderSamplerParseTest, RejectsARegisterThatIsNotADecimalLiteral)
{
    // Fails closed rather than skipping: an unreadable register is exactly the case where the cooker
    // cannot prove the declaration matches the stage the engine binds.
    auto macroRegister = Render::parseShaderSamplerDeclarations("SAMPLER2D(s_mask, MASK_SLOT);");
    ASSERT_FALSE(macroRegister.has_value());
    EXPECT_EQ(macroRegister.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    auto missingRegister = Render::parseShaderSamplerDeclarations("SAMPLER2D(s_mask);");
    ASSERT_FALSE(missingRegister.has_value());
    EXPECT_EQ(missingRegister.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    auto beyondHardware = Render::parseShaderSamplerDeclarations("SAMPLER2D(s_mask, 64);");
    ASSERT_FALSE(beyondHardware.has_value());
    EXPECT_EQ(beyondHardware.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

TEST(ShaderSamplerParseTest, SkipsDeclarationsInsideALiteralFalseConditional)
{
    // A switched-off block declares nothing. Without this the sampler would be collected and then
    // rejected for never being sampled, failing a shader every compiler accepts.
    constexpr std::string_view source = R"(#if 0
SAMPLER2D(s_dead, 9);
#endif
SAMPLER2D(s_mask, 2);
void main() { gl_FragColor = texture2D(s_mask, uv); }
)";
    auto table = Render::parseShaderSamplerDeclarations(source);
    ASSERT_TRUE(table.has_value());
    ASSERT_EQ(table->count, 1U);
    EXPECT_EQ(declaredName(*table, 0), "s_mask");
    EXPECT_EQ(table->declarations[0].stage, 2U);
    EXPECT_TRUE(table->declarations[0].referenced);
}

TEST(ShaderSamplerParseTest, KeepsDeclarationsInConditionalsItCannotEvaluate)
{
    // Only the literal `#if 0` is treated as dead. Any other condition depends on macro state this
    // scan does not model, and keeping the declaration is the safe direction: a wrongly-kept one is
    // checked against a rule the author can read, while a wrongly-dropped one puts a stage mismatch
    // back on the GPU where nothing reports it.
    constexpr std::string_view live = R"(#if 1
SAMPLER2D(s_mask, 2);
#endif
void main() { gl_FragColor = texture2D(s_mask, uv); }
)";
    auto liveTable = Render::parseShaderSamplerDeclarations(live);
    ASSERT_TRUE(liveTable.has_value());
    ASSERT_EQ(liveTable->count, 1U);
    EXPECT_EQ(declaredName(*liveTable, 0), "s_mask");

    // `#if 0x0` and `#if 00` are not the recognised shape, so they stay live rather than being
    // guessed at.
    constexpr std::string_view unevaluated = R"(#ifdef SOMETHING
SAMPLER2D(s_a, 2);
#endif
#if 00
SAMPLER2D(s_b, 3);
#endif
void main() { gl_FragColor = texture2D(s_a, uv) * texture2D(s_b, uv); }
)";
    auto unevaluatedTable = Render::parseShaderSamplerDeclarations(unevaluated);
    ASSERT_TRUE(unevaluatedTable.has_value());
    EXPECT_EQ(unevaluatedTable->count, 2U);
}

TEST(ShaderSamplerParseTest, TracksNestingAndElseInsideADeadConditional)
{
    // A conditional inside a dead block is dead whatever it says, and the `#else` of a dead branch is
    // live. Getting either wrong silently changes which declarations the register rule sees.
    constexpr std::string_view source = R"(#if 0
SAMPLER2D(s_dead, 9);
#ifdef NESTED
SAMPLER2D(s_alsoDead, 8);
#endif
#else
SAMPLER2D(s_mask, 2);
#endif
void main() { gl_FragColor = texture2D(s_mask, uv); }
)";
    auto table = Render::parseShaderSamplerDeclarations(source);
    ASSERT_TRUE(table.has_value());
    ASSERT_EQ(table->count, 1U);
    EXPECT_EQ(declaredName(*table, 0), "s_mask");
    EXPECT_TRUE(table->declarations[0].referenced);
}

TEST(ShaderSamplerRegisterTest, AcceptsEngineSamplersFollowedByConsecutiveAuthorStages)
{
    auto table = Render::parseShaderSamplerDeclarations(
        "SAMPLER2D(s_tex, 0); SAMPLER2D(s_normalTex, 1); SAMPLER2D(s_mask, 2); SAMPLER2D(s_glow, 3);"
        "void main() { gl_FragColor = texture2D(s_mask, uv) * texture2D(s_glow, uv); }");
    ASSERT_TRUE(table.has_value());
    EXPECT_TRUE(
        Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Sprite2D, *table).has_value());

    // An empty table is the common case: a shader that only includes the contract header declares no
    // samplers of its own and must still cook.
    EXPECT_TRUE(Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Sprite2D,
                                                       Render::GpuShaderSamplerDeclarationTable{})
                    .has_value());
}

TEST(ShaderSamplerRegisterTest, RejectsAnAuthorSamplerThatSkipsTheFirstFreeStage)
{
    // Stage 2 is the first stage a Sprite2D author may use, so a shader declaring 3 would be bound at
    // 2 by the backend and sample whatever the engine left there. This is the defect the check exists
    // for: it compiles on every profile and is only wrong on the GPU.
    auto table =
        Render::parseShaderSamplerDeclarations("SAMPLER2D(s_mask, 3); vec4 c = texture2D(s_mask, uv);");
    ASSERT_TRUE(table.has_value());
    auto status = Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Sprite2D, *table);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidShaderUpload);
    // The message has to name the register the author should have written: the only other way to find
    // it is to read the backend.
    EXPECT_NE(status.error().message.find("declare it as 2"), std::string::npos);
}

TEST(ShaderSamplerRegisterTest, RejectsAnEngineSamplerRedeclaredAtTheWrongRegister)
{
    auto table = Render::parseShaderSamplerDeclarations(
        "SAMPLER2D(s_normalTex, 5); vec4 c = texture2D(s_normalTex, uv);");
    ASSERT_TRUE(table.has_value());
    auto status = Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Sprite2D, *table);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidShaderUpload);
    EXPECT_NE(status.error().message.find("register 1"), std::string::npos);
}

TEST(ShaderSamplerRegisterTest, CountsEngineSamplersOutOfTheAuthorSequence)
{
    // The engine sampler sits between the author's two, which must not consume an author slot: the
    // backend subtracts engine uniforms by handle before assigning stages, so s_glow is still the
    // author's second sampler and belongs at 3.
    auto table = Render::parseShaderSamplerDeclarations(
        "SAMPLER2D(s_mask, 2); SAMPLER2D(s_tex, 0); SAMPLER2D(s_glow, 3);"
        "vec4 c = texture2D(s_mask, uv) + texture2D(s_glow, uv);");
    ASSERT_TRUE(table.has_value());
    EXPECT_TRUE(
        Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Sprite2D, *table).has_value());
}

TEST(ShaderSamplerRegisterTest, RejectsADeclaredButNeverSampledSampler)
{
    // The compiler drops s_unused, so the backend numbers s_used at stage 2 while the source says 3:
    // the shader samples whatever the engine last left at 2. Every register in this source is
    // consecutive and correct-looking, which is why a register-only check would pass it.
    auto table = Render::parseShaderSamplerDeclarations(
        "SAMPLER2D(s_unused, 2); SAMPLER2D(s_used, 3); vec4 c = texture2D(s_used, uv);");
    ASSERT_TRUE(table.has_value());
    ASSERT_EQ(table->count, 2U);
    EXPECT_FALSE(table->declarations[0].referenced);
    EXPECT_TRUE(table->declarations[1].referenced);

    auto status = Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Sprite2D, *table);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidShaderUpload);
    EXPECT_NE(status.error().message.find("never sampled"), std::string::npos);

    // An unused *engine* sampler is fine: the engine set is subtracted by handle, so it never occupies
    // an author slot whether the author samples it or not.
    auto engineOnly = Render::parseShaderSamplerDeclarations(
        "SAMPLER2D(s_tex, 0); SAMPLER2D(s_mask, 2); vec4 c = texture2D(s_mask, uv);");
    ASSERT_TRUE(engineOnly.has_value());
    EXPECT_TRUE(Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Sprite2D, *engineOnly)
                    .has_value());
}

TEST(ShaderSamplerRegisterTest, RejectsAuthorSamplersDeclaredOutOfRegisterOrder)
{
    // Both registers are in the free window and neither is duplicated, but the order disagrees with
    // the order the backend assigns stages in. Measured, not assumed: a DXBC uniform table is sorted
    // by register while SPIR-V keeps source order, so this source binds correctly on D3D11 and
    // swaps the two textures on Vulkan.
    auto table = Render::parseShaderSamplerDeclarations(
        "SAMPLER2D(s_high, 3); SAMPLER2D(s_low, 2);"
        "vec4 c = texture2D(s_high, uv) + texture2D(s_low, uv);");
    ASSERT_TRUE(table.has_value());
    auto status = Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Sprite2D, *table);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidShaderUpload);
    EXPECT_NE(status.error().message.find("declare it as 2"), std::string::npos);
}

TEST(ShaderSamplerRegisterTest, AppliesTheMesh3DStageWindowAndCeiling)
{
    // Mesh3D's engine set occupies 0..13, so an author's first sampler is 14 -- the same source that
    // is correct for Sprite2D is wrong here, which is why the check takes the kind.
    auto sprite2DShaped =
        Render::parseShaderSamplerDeclarations("SAMPLER2D(s_mask, 2); vec4 c = texture2D(s_mask, uv);");
    ASSERT_TRUE(sprite2DShaped.has_value());
    auto wrongKind =
        Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Mesh3D, *sprite2DShaped);
    ASSERT_FALSE(wrongKind.has_value());
    EXPECT_NE(wrongKind.error().message.find("declare it as 14"), std::string::npos);

    auto mesh3DShaped = Render::parseShaderSamplerDeclarations(
        "SAMPLER2D(s_mask, 14); SAMPLER2D(s_glow, 15);"
        "vec4 c = texture2D(s_mask, uv) + texture2D(s_glow, uv);");
    ASSERT_TRUE(mesh3DShaped.has_value());
    EXPECT_TRUE(
        Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Mesh3D, *mesh3DShaped).has_value());

    // Two is all Mesh3D has: bgfx binds 16 textures per draw and the engine set holds 14.
    auto overCeiling = Render::parseShaderSamplerDeclarations(
        "SAMPLER2D(s_mask, 14); SAMPLER2D(s_glow, 15); SAMPLER2D(s_extra, 16);"
        "vec4 c = texture2D(s_mask, uv) + texture2D(s_glow, uv) + texture2D(s_extra, uv);");
    ASSERT_TRUE(overCeiling.has_value());
    auto status = Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Mesh3D, *overCeiling);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

TEST(ShaderSamplerRegisterTest, RejectsADuplicateSamplerNameAndAnUnsupportedKind)
{
    auto table = Render::parseShaderSamplerDeclarations(
        "SAMPLER2D(s_mask, 2); SAMPLER2D(s_mask, 3); vec4 c = texture2D(s_mask, uv);");
    ASSERT_TRUE(table.has_value());
    auto duplicate = Render::validateAuthorSamplerRegisters(Render::GpuShaderKind::Sprite2D, *table);
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, Render::RenderErrorCode::InvalidShaderUpload);

    auto invalidKind = Render::validateAuthorSamplerRegisters(
        Render::GpuShaderKind::Invalid, Render::GpuShaderSamplerDeclarationTable{});
    ASSERT_FALSE(invalidKind.has_value());
    EXPECT_EQ(invalidKind.error().code, Render::RenderErrorCode::InvalidShaderUpload);
}

} // namespace Tina::Tests
