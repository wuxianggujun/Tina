#include <gtest/gtest.h>

#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>

#include <array>
#include <cstddef>
#include <limits>

namespace Tina::Tests {
namespace {

constexpr std::array<std::byte, 4> ParameterSets{std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0x67}};

// A desc that passes every rule, so each test can break exactly one field and
// attribute the rejection to it.
[[nodiscard]] Render::VideoDecodeTextureDesc validDesc() noexcept
{
    return Render::VideoDecodeTextureDesc{
        .codec = Render::VideoCodec::H264,
        .chroma = Render::VideoChromaSubsampling::Yuv420,
        .bitDepth = 8,
        .codedWidth = 1920,
        .codedHeight = 1080,
        .maxDpbSlots = 4,
        .maxActiveReferences = 2,
        .parameterSets = ParameterSets,
    };
}

} // namespace

TEST(VideoDecodeTextureDescTest, AFullyPopulatedDescIsAccepted)
{
    EXPECT_TRUE(Render::validateVideoDecodeTextureDesc(validDesc()));
}

TEST(VideoDecodeTextureDescTest, AnUnknownCodecIsRejected)
{
    auto desc = validDesc();
    desc.codec = static_cast<Render::VideoCodec>(0xFF);

    const auto status = Render::validateVideoDecodeTextureDesc(desc);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
}

TEST(VideoDecodeTextureDescTest, EveryNamedCodecIsAccepted)
{
    for (const Render::VideoCodec codec :
         {Render::VideoCodec::H264, Render::VideoCodec::H265, Render::VideoCodec::Av1})
    {
        auto desc = validDesc();
        desc.codec = codec;
        EXPECT_TRUE(Render::validateVideoDecodeTextureDesc(desc))
            << "codec " << static_cast<int>(codec) << " is declared but rejected";
    }
}

TEST(VideoDecodeTextureDescTest, AnUnknownChromaSubsamplingIsRejected)
{
    auto desc = validDesc();
    desc.chroma = static_cast<Render::VideoChromaSubsampling>(0xFF);

    const auto status = Render::validateVideoDecodeTextureDesc(desc);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
}

TEST(VideoDecodeTextureDescTest, EveryNamedChromaSubsamplingIsAccepted)
{
    for (const Render::VideoChromaSubsampling chroma : {Render::VideoChromaSubsampling::Yuv420,
                                                        Render::VideoChromaSubsampling::Yuv422,
                                                        Render::VideoChromaSubsampling::Yuv444})
    {
        auto desc = validDesc();
        desc.chroma = chroma;
        EXPECT_TRUE(Render::validateVideoDecodeTextureDesc(desc))
            << "chroma " << static_cast<int>(chroma) << " is declared but rejected";
    }
}

TEST(VideoDecodeTextureDescTest, OnlyEightTenAndTwelveBitDepthsAreAccepted)
{
    for (const u8 bitDepth : {u8{8}, u8{10}, u8{12}})
    {
        auto desc = validDesc();
        desc.bitDepth = bitDepth;
        EXPECT_TRUE(Render::validateVideoDecodeTextureDesc(desc))
            << "bit depth " << static_cast<int>(bitDepth) << " is documented but rejected";
    }

    for (const u8 bitDepth : {u8{0}, u8{1}, u8{9}, u8{11}, u8{16}, u8{255}})
    {
        auto desc = validDesc();
        desc.bitDepth = bitDepth;
        const auto status = Render::validateVideoDecodeTextureDesc(desc);
        ASSERT_FALSE(status) << "bit depth " << static_cast<int>(bitDepth) << " must be rejected";
        EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
    }
}

TEST(VideoDecodeTextureDescTest, ZeroCodedDimensionsAreRejected)
{
    {
        auto desc = validDesc();
        desc.codedWidth = 0;
        const auto status = Render::validateVideoDecodeTextureDesc(desc);
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
    }
    {
        auto desc = validDesc();
        desc.codedHeight = 0;
        const auto status = Render::validateVideoDecodeTextureDesc(desc);
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
    }
}

// The advertised maximum must itself be usable. A guard written with >= instead of >
// would make the documented limit the first rejected value, which a caller sizing a
// stream to exactly MaximumDimension would hit.
TEST(VideoDecodeTextureDescTest, TheMaximumCodedDimensionIsItselfAccepted)
{
    auto desc = validDesc();
    desc.codedWidth = Render::VideoDecodeTextureDesc::MaximumDimension;
    desc.codedHeight = Render::VideoDecodeTextureDesc::MaximumDimension;

    EXPECT_TRUE(Render::validateVideoDecodeTextureDesc(desc))
        << "MaximumDimension is advertised as supported and must be deliverable";
}

TEST(VideoDecodeTextureDescTest, ACodedDimensionAboveTheMaximumIsRejected)
{
    constexpr u16 AboveMaximum = Render::VideoDecodeTextureDesc::MaximumDimension + 1U;
    static_assert(AboveMaximum > Render::VideoDecodeTextureDesc::MaximumDimension,
                  "the boundary must be representable in the field's own type");

    {
        auto desc = validDesc();
        desc.codedWidth = AboveMaximum;
        const auto status = Render::validateVideoDecodeTextureDesc(desc);
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
    }
    {
        auto desc = validDesc();
        desc.codedHeight = AboveMaximum;
        const auto status = Render::validateVideoDecodeTextureDesc(desc);
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
    }
}

// Zero slots would leave the reference-picture budget to the device, so the same
// stream would behave differently per backend.
TEST(VideoDecodeTextureDescTest, ZeroDecodedPictureBufferSlotsAreRejected)
{
    auto desc = validDesc();
    desc.maxDpbSlots = 0;
    desc.maxActiveReferences = 0;

    const auto status = Render::validateVideoDecodeTextureDesc(desc);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
}

TEST(VideoDecodeTextureDescTest, MoreActiveReferencesThanSlotsIsRejected)
{
    auto desc = validDesc();
    desc.maxDpbSlots = 2;
    desc.maxActiveReferences = 3;

    const auto status = Render::validateVideoDecodeTextureDesc(desc);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
}

TEST(VideoDecodeTextureDescTest, AsManyActiveReferencesAsSlotsIsAccepted)
{
    auto desc = validDesc();
    desc.maxDpbSlots = 3;
    desc.maxActiveReferences = 3;

    EXPECT_TRUE(Render::validateVideoDecodeTextureDesc(desc));
}

TEST(VideoDecodeTextureDescTest, AStreamShapeNeedsNoParameterSets)
{
    // isVideoDecodeSupported asks about the device, not a particular stream's headers,
    // so a caller must be able to ask without fabricating one.
    auto desc = validDesc();
    desc.parameterSets = {};

    EXPECT_TRUE(Render::validateVideoDecodeTextureDesc(desc));
}

TEST(VideoDecodeTextureCreationTest, CreationRequiresTheCodecParameterSets)
{
    auto desc = validDesc();
    desc.parameterSets = {};

    const auto status = Render::validateVideoDecodeTextureCreation(desc);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
}

TEST(VideoDecodeTextureCreationTest, CreationAcceptsAFullyPopulatedDesc)
{
    EXPECT_TRUE(Render::validateVideoDecodeTextureCreation(validDesc()));
}

// Creation must not skip the shape rules just because the parameter sets are present.
TEST(VideoDecodeTextureCreationTest, CreationStillEnforcesTheStreamShape)
{
    auto desc = validDesc();
    desc.maxDpbSlots = 0;
    desc.maxActiveReferences = 0;

    const auto status = Render::validateVideoDecodeTextureCreation(desc);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::VideoCodecUnsupported);
}

TEST(VideoDecodeSubmissionTest, AClockOnlyTickCarriesNoAccessUnitsAndNoBitstream)
{
    const Render::VideoDecodeSubmission submission{.presentationTimeUs = 33'000};

    EXPECT_TRUE(Render::validateVideoDecodeSubmission(submission));
}

// A batch built without its access-unit table would silently decode nothing, so the
// bitstream must not survive the table going missing.
TEST(VideoDecodeSubmissionTest, AClockOnlyTickCarryingABitstreamIsRejected)
{
    constexpr std::array<std::byte, 2> Bytes{std::byte{1}, std::byte{2}};
    const Render::VideoDecodeSubmission submission{.bitstream = Bytes};

    const auto status = Render::validateVideoDecodeSubmission(submission);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidVideoDecodeSubmission);
}

TEST(VideoDecodeSubmissionTest, ASeekDiscontinuityWithNoAccessUnitsIsRejected)
{
    const Render::VideoDecodeSubmission submission{.isSeekDiscontinuity = true};

    const auto status = Render::validateVideoDecodeSubmission(submission);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidVideoDecodeSubmission);
}

TEST(VideoDecodeSubmissionTest, AccessUnitsWithoutTheirBitstreamAreRejected)
{
    constexpr std::array<Render::VideoDecodeAccessUnit, 1> Units{
        Render::VideoDecodeAccessUnit{.byteSize = 4, .presentationTimeUs = 0}};
    const Render::VideoDecodeSubmission submission{.accessUnits = Units};

    const auto status = Render::validateVideoDecodeSubmission(submission);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidVideoDecodeSubmission);
}

TEST(VideoDecodeSubmissionTest, AZeroSizedAccessUnitIsRejected)
{
    constexpr std::array<std::byte, 2> Bytes{std::byte{1}, std::byte{2}};
    constexpr std::array<Render::VideoDecodeAccessUnit, 2> Units{
        Render::VideoDecodeAccessUnit{.byteSize = 2, .presentationTimeUs = 0},
        Render::VideoDecodeAccessUnit{.byteSize = 0, .presentationTimeUs = 33'000}};
    const Render::VideoDecodeSubmission submission{.bitstream = Bytes, .accessUnits = Units};

    const auto status = Render::validateVideoDecodeSubmission(submission);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidVideoDecodeSubmission);
}

// The sizes index into the bitstream, so a short sum drops the tail and a long one
// reads past the end.
TEST(VideoDecodeSubmissionTest, AccessUnitSizesMustSumToExactlyTheBitstreamSize)
{
    constexpr std::array<std::byte, 4> Bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};

    {
        constexpr std::array<Render::VideoDecodeAccessUnit, 1> Short{
            Render::VideoDecodeAccessUnit{.byteSize = 3, .presentationTimeUs = 0}};
        const Render::VideoDecodeSubmission submission{.bitstream = Bytes, .accessUnits = Short};
        const auto status = Render::validateVideoDecodeSubmission(submission);
        ASSERT_FALSE(status) << "a sum below the bitstream size drops the tail";
        EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidVideoDecodeSubmission);
    }
    {
        constexpr std::array<Render::VideoDecodeAccessUnit, 1> Long{
            Render::VideoDecodeAccessUnit{.byteSize = 5, .presentationTimeUs = 0}};
        const Render::VideoDecodeSubmission submission{.bitstream = Bytes, .accessUnits = Long};
        const auto status = Render::validateVideoDecodeSubmission(submission);
        ASSERT_FALSE(status) << "a sum above the bitstream size reads past the end";
        EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidVideoDecodeSubmission);
    }
}

TEST(VideoDecodeSubmissionTest, SeveralAccessUnitsSummingToTheBitstreamAreAccepted)
{
    constexpr std::array<std::byte, 6> Bytes{std::byte{1}, std::byte{2}, std::byte{3},
                                             std::byte{4}, std::byte{5}, std::byte{6}};
    constexpr std::array<Render::VideoDecodeAccessUnit, 3> Units{
        Render::VideoDecodeAccessUnit{.byteSize = 1, .presentationTimeUs = 0},
        Render::VideoDecodeAccessUnit{.byteSize = 2, .presentationTimeUs = 33'000},
        Render::VideoDecodeAccessUnit{.byteSize = 3, .presentationTimeUs = 66'000}};
    const Render::VideoDecodeSubmission submission{.bitstream = Bytes, .accessUnits = Units};

    EXPECT_TRUE(Render::validateVideoDecodeSubmission(submission));
}

TEST(VideoDecodeSubmissionTest, ASeekDiscontinuityCarryingItsAccessUnitsIsAccepted)
{
    constexpr std::array<std::byte, 3> Bytes{std::byte{1}, std::byte{2}, std::byte{3}};
    constexpr std::array<Render::VideoDecodeAccessUnit, 1> Units{
        Render::VideoDecodeAccessUnit{.byteSize = 3, .presentationTimeUs = 0}};
    const Render::VideoDecodeSubmission submission{
        .bitstream = Bytes, .accessUnits = Units, .presentationTimeUs = 0, .isSeekDiscontinuity = true};

    EXPECT_TRUE(Render::validateVideoDecodeSubmission(submission));
}

// The presentation flags are orthogonal to the bitstream rules: a pre-roll batch and a
// final batch must validate on the same terms as any other.
TEST(VideoDecodeSubmissionTest, PresentationFlagsDoNotChangeTheBitstreamRules)
{
    constexpr std::array<std::byte, 2> Bytes{std::byte{1}, std::byte{2}};
    constexpr std::array<Render::VideoDecodeAccessUnit, 1> Units{
        Render::VideoDecodeAccessUnit{.byteSize = 2, .presentationTimeUs = 0}};

    Render::VideoDecodeSubmission submission{.bitstream = Bytes, .accessUnits = Units};
    submission.suppressPresentation = true;
    submission.isFinalAccessUnit = true;

    EXPECT_TRUE(Render::validateVideoDecodeSubmission(submission));
}

} // namespace Tina::Tests
