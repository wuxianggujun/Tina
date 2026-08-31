#include <tina/editor/EditorViewportPick.hpp>

#include <optional>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Editor::EditorViewportPickCandidate>);
static_assert(std::is_trivially_copyable_v<Tina::Editor::EditorViewportPickHit>);
static_assert(std::is_trivially_copyable_v<Tina::Editor::EditorViewportRayQuery>);

// Fixed capacity, same bound the marquee candidate surface uses.
static_assert(Tina::Editor::EditorViewportPickCandidateCapacity == 64U);

// A miss is a normal outcome, so both entry points report absence through optional
// rather than an allocating Core::Error.
static_assert(std::is_same_v<
              decltype(Tina::Editor::editorViewportPickRay(
                  Tina::Editor::EditorViewportRayQuery{})),
              std::optional<Tina::Math::Ray>>);
static_assert(std::is_same_v<
              decltype(Tina::Editor::pickNearestViewportCandidate(
                  Tina::Math::Ray{},
                  std::span<const Tina::Editor::EditorViewportPickCandidate>{})),
              std::optional<Tina::Editor::EditorViewportPickHit>>);
static_assert(noexcept(Tina::Editor::editorViewportPickRay(
    Tina::Editor::EditorViewportRayQuery{})));
static_assert(noexcept(Tina::Editor::pickNearestViewportCandidate(
    Tina::Math::Ray{}, std::span<const Tina::Editor::EditorViewportPickCandidate>{})));

// The candidate publishes a full sphere, center included. Reducing it to a radius
// is what put the old hot zone at the wrong place for offset bounds.
static_assert(sizeof(Tina::Editor::EditorViewportPickCandidate) >=
              sizeof(Tina::Math::Sphere));
