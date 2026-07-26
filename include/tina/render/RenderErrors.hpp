#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Render::RenderErrorCode {

inline constexpr Core::ErrorCode DeviceStopped{Core::ErrorDomain::Render, 1};
inline constexpr Core::ErrorCode FrameAlreadyOpen{Core::ErrorDomain::Render, 2};
inline constexpr Core::ErrorCode NoFrameSubmitted{Core::ErrorDomain::Render, 3};
inline constexpr Core::ErrorCode UnexpectedFrameIndex{Core::ErrorDomain::Render, 4};
inline constexpr Core::ErrorCode InvalidSurfaceState{Core::ErrorDomain::Render, 5};
inline constexpr Core::ErrorCode SurfaceReconfigureFailed{Core::ErrorDomain::Render, 6};
inline constexpr Core::ErrorCode NativeWindowBindingChangedUnsupported{Core::ErrorDomain::Render, 7};
inline constexpr Core::ErrorCode DeviceInitializationFailed{Core::ErrorDomain::Render, 8};
inline constexpr Core::ErrorCode InvalidNativeWindowBinding{Core::ErrorDomain::Render, 9};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Render, 10};
inline constexpr Core::ErrorCode InvalidDisplayListCapacity{Core::ErrorDomain::Render, 11};
inline constexpr Core::ErrorCode DisplayListStorageAllocationFailed{Core::ErrorDomain::Render, 12};
inline constexpr Core::ErrorCode DisplayListBuildAlreadyOpen{Core::ErrorDomain::Render, 13};
inline constexpr Core::ErrorCode DisplayListBuildNotOpen{Core::ErrorDomain::Render, 14};
inline constexpr Core::ErrorCode DisplayListCapacityExceeded{Core::ErrorDomain::Render, 15};
inline constexpr Core::ErrorCode InvalidPremultipliedColor{Core::ErrorDomain::Render, 16};
inline constexpr Core::ErrorCode InvalidDrawCommand{Core::ErrorDomain::Render, 17};
inline constexpr Core::ErrorCode TransientBufferCapacityExceeded{Core::ErrorDomain::Render, 18};
inline constexpr Core::ErrorCode InvalidRenderSceneCapacity{Core::ErrorDomain::Render, 19};
inline constexpr Core::ErrorCode RenderSceneStorageAllocationFailed{Core::ErrorDomain::Render, 20};
inline constexpr Core::ErrorCode RenderSceneBuildAlreadyOpen{Core::ErrorDomain::Render, 21};
inline constexpr Core::ErrorCode RenderSceneBuildNotOpen{Core::ErrorDomain::Render, 22};
inline constexpr Core::ErrorCode RenderSceneCapacityExceeded{Core::ErrorDomain::Render, 23};
inline constexpr Core::ErrorCode InvalidRenderSceneInput{Core::ErrorDomain::Render, 24};
inline constexpr Core::ErrorCode RenderSceneCameraConflict{Core::ErrorDomain::Render, 25};
inline constexpr Core::ErrorCode RenderSceneMissingCamera{Core::ErrorDomain::Render, 26};
inline constexpr Core::ErrorCode UploadTicketInvalid{Core::ErrorDomain::Render, 27};
inline constexpr Core::ErrorCode UploadTicketNotRetirable{Core::ErrorDomain::Render, 28};
// Shared capacity / argument errors used by FramePin packet and completion ledger.
// (CapacityExceeded may already exist elsewhere in this domain; keep stable codes.)
inline constexpr Core::ErrorCode UploadLedgerFull{Core::ErrorDomain::Render, 29};
inline constexpr Core::ErrorCode TextureUploadUnsupported{Core::ErrorDomain::Render, 30};
inline constexpr Core::ErrorCode InvalidTextureUpload{Core::ErrorDomain::Render, 31};
inline constexpr Core::ErrorCode TextureNotFound{Core::ErrorDomain::Render, 32};
inline constexpr Core::ErrorCode FrameCaptureUnsupported{Core::ErrorDomain::Render, 33};
inline constexpr Core::ErrorCode FrameCaptureFailed{Core::ErrorDomain::Render, 34};
inline constexpr Core::ErrorCode FrameCaptureBusy{Core::ErrorDomain::Render, 35};
inline constexpr Core::ErrorCode MeshUploadUnsupported{Core::ErrorDomain::Render, 36};
inline constexpr Core::ErrorCode InvalidMeshUpload{Core::ErrorDomain::Render, 37};
inline constexpr Core::ErrorCode MeshNotFound{Core::ErrorDomain::Render, 38};
inline constexpr Core::ErrorCode FramePinCapacityExceeded{Core::ErrorDomain::Render, 39};
inline constexpr Core::ErrorCode InvalidFramePin{Core::ErrorDomain::Render, 40};
inline constexpr Core::ErrorCode SubmissionCompletionLedgerFull{Core::ErrorDomain::Render, 41};
inline constexpr Core::ErrorCode InvalidSubmissionTicket{Core::ErrorDomain::Render, 42};
inline constexpr Core::ErrorCode InvalidMesh3DLighting{Core::ErrorDomain::Render, 43};
inline constexpr Core::ErrorCode GpuRetirementUnsupported{Core::ErrorDomain::Render, 44};
inline constexpr Core::ErrorCode GpuRetirementDrainFailed{Core::ErrorDomain::Render, 45};
inline constexpr Core::ErrorCode SpriteBindingKeyExhausted{Core::ErrorDomain::Render, 46};
inline constexpr Core::ErrorCode Mesh3DBindingKeyExhausted{Core::ErrorDomain::Render, 47};
inline constexpr Core::ErrorCode Mesh3DMaterialBindingKeyExhausted{Core::ErrorDomain::Render, 48};

} // namespace Tina::Render::RenderErrorCode
