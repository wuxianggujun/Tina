#pragma once

#include <tina/asset/CatalogCook.hpp>
#include <tina/core/error/Result.hpp>

#include <string_view>

namespace Tina::Asset {

// One-step media importers for the source import pipeline.
//
// cookTextureFileToCatalogSourceResult: one PNG/JPEG image file cooks into one
// Rgba8Unorm Texture2D. Sprite2D resolution accepts this imported Texture2D
// directly; explicitly authored Sprite assets remain supported as wrappers.
// The AssetId is derived deterministically from the canonical source-root-
// relative path, so renaming the file changes its output identity.
//
// cookAudioFileToCatalogSourceResult: one PCM16 RIFF/WAVE file cooks into one
// AudioClip with a path-derived AssetId. Other codecs fail closed.
[[nodiscard]] Core::Result<CatalogCookSourceResult>
cookTextureFileToCatalogSourceResult(std::string_view imageUtf8Path,
                                     AssetFormat::TargetPlatform targetPlatform,
                                     SourceImportCaptureConfig captureConfig) noexcept;

[[nodiscard]] Core::Result<CatalogCookSourceResult>
cookAudioFileToCatalogSourceResult(std::string_view wavUtf8Path,
                                   AssetFormat::TargetPlatform targetPlatform,
                                   SourceImportCaptureConfig captureConfig) noexcept;

} // namespace Tina::Asset
