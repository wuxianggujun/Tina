#pragma once

// Where a sample that still cooks at startup puts its catalog.
//
// Below the executable, never the system temporary directory. Two reasons, both observed
// here: this host's AppData-backed temp tree cannot create the AF_UNIX socket pair a
// Selector needs, so anything reaching into %TEMP% is one machine-specific failure away
// from an error that names neither the sample nor the directory; and a catalog under
// %TEMP% outlives nothing -- it is deleted independently of the build tree, so a rerun
// silently re-cooks while a stale sibling from a previous build looks identical.
//
// The relative name matches tina_sample_runtime_cook_dir() in cmake/TinaGameProject.cmake,
// which is what a build-time cook would stage into. Keeping the two spellings identical is
// what makes moving a sample from startup cooking to tina_cook_catalog() a CMakeLists-only
// change.

#include <tina/core/error/Error.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/io/ApplicationPaths.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Tina::Sample {

// Removes and recreates `<executable directory>/<name>`, and returns it.
//
// Recreated rather than reused so a leftover tree from the previous run cannot mix
// objects with the next cook. cookAndPublishCatalogPackage is in-place and would
// accept the old root; wiping is the sample's policy, not the cooker's. `name` is a
// single directory component; nested paths and traversal are rejected by
// applicationFilePath.
[[nodiscard]] inline Core::Result<std::filesystem::path>
prepareApplicationContentDirectory(std::string_view name)
{
    auto resolved = Core::applicationFilePath(name);
    if (!resolved)
    {
        return Core::failure(std::move(resolved.error()));
    }

    const std::filesystem::path root{std::filesystem::path(*resolved)};
    std::error_code removeError;
    std::filesystem::remove_all(root, removeError);
    if (removeError)
    {
        Core::Error error{Core::CoreErrorCode::Io, "failed to clear the sample content directory"};
        error.setNativeCode(removeError.value());
        error.addContext("prepareApplicationContentDirectory", removeError.message());
        error.addContext("root", *resolved);
        return Core::failure(std::move(error));
    }

    std::error_code createError;
    std::filesystem::create_directories(root, createError);
    if (createError)
    {
        Core::Error error{Core::CoreErrorCode::Io, "failed to create the sample content directory"};
        error.setNativeCode(createError.value());
        error.addContext("prepareApplicationContentDirectory", createError.message());
        error.addContext("root", *resolved);
        return Core::failure(std::move(error));
    }
    return root;
}

} // namespace Tina::Sample
