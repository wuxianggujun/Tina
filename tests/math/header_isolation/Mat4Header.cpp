#include <tina/math/Mat4.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Math::Mat4>);
// 16 tightly packed floats, so the buffer reaches the render backend unchanged.
static_assert(sizeof(Tina::Math::Mat4) == 64);

// Default construction is identity.
static_assert(Tina::Math::Mat4{} == Tina::Math::identityMat4());
static_assert(Tina::Math::identityMat4().at(0, 0) == 1.0F);
static_assert(Tina::Math::identityMat4().at(3, 3) == 1.0F);

// COLUMN-MAJOR is the published contract: element(row, column) lives at
// columns[column * 4 + row], so translation occupies indices 12..14. A transposed
// implementation would pass every symmetric test but fail these.
static_assert(Tina::Math::translationMat4(Tina::Math::Vec3{7.0F, 8.0F, 9.0F}).columns[12] == 7.0F);
static_assert(Tina::Math::translationMat4(Tina::Math::Vec3{7.0F, 8.0F, 9.0F}).columns[13] == 8.0F);
static_assert(Tina::Math::translationMat4(Tina::Math::Vec3{7.0F, 8.0F, 9.0F}).columns[14] == 9.0F);
static_assert(Tina::Math::translationMat4(Tina::Math::Vec3{7.0F, 8.0F, 9.0F}).at(0, 3) == 7.0F);

static_assert(Tina::Math::scaleMat4(Tina::Math::Vec3{2.0F, 3.0F, 4.0F}).at(1, 1) == 3.0F);
static_assert(Tina::Math::linearDeterminant(Tina::Math::scaleMat4(
                  Tina::Math::Vec3{2.0F, 3.0F, 4.0F}))
              == 24.0F);

// Points pick up translation; directions do not.
static_assert(Tina::Math::transformPoint(
                  Tina::Math::translationMat4(Tina::Math::Vec3{1.0F, 2.0F, 3.0F}),
                  Tina::Math::Vec3{})
              == Tina::Math::Vec3{1.0F, 2.0F, 3.0F});
static_assert(Tina::Math::transformDirection(
                  Tina::Math::translationMat4(Tina::Math::Vec3{1.0F, 2.0F, 3.0F}),
                  Tina::Math::Vec3{1.0F, 0.0F, 0.0F})
              == Tina::Math::Vec3{1.0F, 0.0F, 0.0F});

// Clip depth range is an explicit parameter, never an assumed default.
static_assert(!std::is_same_v<Tina::Math::ClipDepthRange, bool>);
static_assert(static_cast<Tina::u8>(Tina::Math::ClipDepthRange::ZeroToOne) == 0U);
