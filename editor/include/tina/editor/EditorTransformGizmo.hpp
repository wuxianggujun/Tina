#pragma once

#include <tina/core/base/Types.hpp>

#include <array>
#include <span>

namespace Tina::Editor {

inline constexpr Core::usize EditorTransformGizmoAxisCount = 3;
inline constexpr Core::usize EditorTransformGizmoHandleCapacity = 7;

enum class EditorTransformGizmoDimension : Core::u8 {
    TwoD,
    ThreeD,
};

enum class EditorTransformGizmoMode : Core::u8 {
    Translate,
    Rotate,
    Scale,
};

enum class EditorTransformGizmoOrientation : Core::u8 {
    World,
    Local,
};

enum class EditorTransformGizmoHandle : Core::u8 {
    None,
    AxisX,
    AxisY,
    AxisZ,
    PlaneXY,
    PlaneXZ,
    PlaneYZ,
    Uniform,
};

enum class EditorTransformGizmoHandleShape : Core::u8 {
    Segment,
    Quad,
    Ring,
};

enum class EditorTransformGizmoOperation : Core::u8 {
    Success,
    InvalidInput,
    DragAlreadyActive,
    NoActiveDrag,
    PointerMismatch,
    NoHandleAtPointer,
};

struct EditorTransformGizmoPoint final {
    float x = 0.0F;
    float y = 0.0F;

    friend bool operator==(const EditorTransformGizmoPoint&,
                           const EditorTransformGizmoPoint&) = default;
};

struct EditorTransformGizmoVector final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend bool operator==(const EditorTransformGizmoVector&,
                           const EditorTransformGizmoVector&) = default;
};

// screenPerWorldUnit is the projected screen-space displacement of one world
// unit. worldDirection identifies the corresponding normalized transform axis.
struct EditorTransformGizmoAxisProjection final {
    EditorTransformGizmoPoint screenPerWorldUnit{};
    EditorTransformGizmoVector worldDirection{};

    friend bool operator==(const EditorTransformGizmoAxisProjection&,
                           const EditorTransformGizmoAxisProjection&) = default;
};

struct EditorTransformGizmoFrame final {
    EditorTransformGizmoDimension dimension = EditorTransformGizmoDimension::TwoD;
    EditorTransformGizmoPoint screenOrigin{};
    std::array<EditorTransformGizmoAxisProjection, EditorTransformGizmoAxisCount>
        worldAxes{};
    std::array<EditorTransformGizmoAxisProjection, EditorTransformGizmoAxisCount>
        localAxes{};

    friend bool operator==(const EditorTransformGizmoFrame&,
                           const EditorTransformGizmoFrame&) = default;
};

struct EditorTransformGizmoConfig final {
    float axisLengthPixels = 72.0F;
    float axisHitRadiusPixels = 6.0F;
    float planeOffsetPixels = 16.0F;
    float planeExtentPixels = 18.0F;
    float rotationRadiusPixels = 58.0F;
    float rotationRingSpacingPixels = 12.0F;
    float rotationHitRadiusPixels = 5.0F;
    float uniformHandleExtentPixels = 10.0F;
    float uniformScalePixelsPerUnit = 80.0F;
    float minimumScaleFactor = 0.001F;
    float maximumScaleFactor = 1'000.0F;

    friend bool operator==(const EditorTransformGizmoConfig&,
                           const EditorTransformGizmoConfig&) = default;
};

struct EditorTransformGizmoSnap final {
    bool enabled = false;
    float translationStep = 1.0F;
    float rotationStepDegrees = 15.0F;
    float scaleStep = 0.1F;

    friend bool operator==(const EditorTransformGizmoSnap&,
                           const EditorTransformGizmoSnap&) = default;
};

struct EditorTransformGizmoHandleGeometry final {
    EditorTransformGizmoHandle handle = EditorTransformGizmoHandle::None;
    EditorTransformGizmoHandleShape shape =
        EditorTransformGizmoHandleShape::Segment;
    std::array<EditorTransformGizmoPoint, 4> points{};
    Core::u8 pointCount = 0;
    float radiusPixels = 0.0F;
    float hitRadiusPixels = 0.0F;

    friend bool operator==(const EditorTransformGizmoHandleGeometry&,
                           const EditorTransformGizmoHandleGeometry&) = default;
};

// The owner applies this delta to its canonical document transaction. Rotation
// is axis-angle rather than Euler so world/local orientation remains explicit.
struct EditorTransformGizmoDelta final {
    EditorTransformGizmoHandle handle = EditorTransformGizmoHandle::None;
    EditorTransformGizmoVector translation{};
    EditorTransformGizmoVector rotationAxis{};
    float rotationDegrees = 0.0F;
    EditorTransformGizmoVector scaleFactors{1.0F, 1.0F, 1.0F};
    EditorTransformGizmoOrientation orientation =
        EditorTransformGizmoOrientation::World;

    friend bool operator==(const EditorTransformGizmoDelta&,
                           const EditorTransformGizmoDelta&) = default;
};

// Fixed-capacity, owning publication. handles() and the returned snapshot stay
// valid until the next successful gizmo operation; no interaction allocates.
struct EditorTransformGizmoSnapshot final {
    Core::u64 revision = 0;
    bool framePublished = false;
    EditorTransformGizmoDimension dimension = EditorTransformGizmoDimension::TwoD;
    EditorTransformGizmoMode mode = EditorTransformGizmoMode::Translate;
    EditorTransformGizmoOrientation orientation =
        EditorTransformGizmoOrientation::World;
    EditorTransformGizmoHandle hoveredHandle = EditorTransformGizmoHandle::None;
    EditorTransformGizmoHandle activeHandle = EditorTransformGizmoHandle::None;
    Core::u64 activePointer = 0;
    EditorTransformGizmoPoint dragStart{};
    EditorTransformGizmoPoint dragCurrent{};
    EditorTransformGizmoDelta delta{};
    std::array<EditorTransformGizmoHandleGeometry,
               EditorTransformGizmoHandleCapacity>
        handleStorage{};
    Core::u8 handleCount = 0;

    [[nodiscard]] bool dragging() const noexcept
    {
        return activeHandle != EditorTransformGizmoHandle::None;
    }

    [[nodiscard]] std::span<const EditorTransformGizmoHandleGeometry>
    handles() const noexcept
    {
        return std::span(handleStorage.data(), handleCount);
    }
};

// Backend-neutral transform interaction state. The caller supplies projected
// world/local axes, renders the fixed snapshot, and commits the final delta.
class EditorTransformGizmo final {
  public:
    [[nodiscard]] EditorTransformGizmoOperation
    configure(const EditorTransformGizmoConfig& config) noexcept;
    [[nodiscard]] EditorTransformGizmoOperation
    setMode(EditorTransformGizmoMode mode) noexcept;
    [[nodiscard]] EditorTransformGizmoOperation
    setOrientation(EditorTransformGizmoOrientation orientation) noexcept;
    [[nodiscard]] EditorTransformGizmoOperation
    setSnap(const EditorTransformGizmoSnap& snap) noexcept;
    [[nodiscard]] EditorTransformGizmoOperation
    publishFrame(const EditorTransformGizmoFrame& frame) noexcept;

    [[nodiscard]] EditorTransformGizmoHandle
    hitTest(EditorTransformGizmoPoint pointer) const noexcept;
    [[nodiscard]] EditorTransformGizmoOperation
    updateHover(EditorTransformGizmoPoint pointer) noexcept;
    [[nodiscard]] EditorTransformGizmoOperation
    beginDrag(Core::u64 pointer, EditorTransformGizmoPoint position) noexcept;
    [[nodiscard]] EditorTransformGizmoOperation
    updateDrag(Core::u64 pointer, EditorTransformGizmoPoint position) noexcept;
    [[nodiscard]] EditorTransformGizmoOperation
    endDrag(Core::u64 pointer, EditorTransformGizmoPoint position) noexcept;
    [[nodiscard]] EditorTransformGizmoOperation cancelDrag(Core::u64 pointer) noexcept;

    [[nodiscard]] const EditorTransformGizmoConfig& config() const noexcept
    {
        return m_config;
    }

    [[nodiscard]] const EditorTransformGizmoSnap& snap() const noexcept
    {
        return m_snap;
    }

    [[nodiscard]] const EditorTransformGizmoSnapshot& snapshot() const noexcept
    {
        return m_snapshot;
    }

  private:
    struct ResolvedAxis final {
        EditorTransformGizmoPoint screenPerWorldUnit{};
        EditorTransformGizmoVector worldDirection{};
        bool screenUsable = false;
    };

    [[nodiscard]] EditorTransformGizmoOperation rebuild(
        const EditorTransformGizmoConfig& config,
        EditorTransformGizmoMode mode,
        EditorTransformGizmoOrientation orientation,
        const EditorTransformGizmoFrame& frame) noexcept;
    [[nodiscard]] bool computeDragDelta(EditorTransformGizmoPoint position,
                                        EditorTransformGizmoDelta& delta) const noexcept;
    void advanceRevision() noexcept;

    EditorTransformGizmoConfig m_config{};
    EditorTransformGizmoSnap m_snap{};
    EditorTransformGizmoFrame m_frame{};
    std::array<ResolvedAxis, EditorTransformGizmoAxisCount> m_resolvedAxes{};
    EditorTransformGizmoSnapshot m_snapshot{};
};

} // namespace Tina::Editor
