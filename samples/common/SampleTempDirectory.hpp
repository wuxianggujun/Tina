#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/error/Result.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Tina::Sample {

[[nodiscard]] inline Core::Result<std::filesystem::path>
createUniqueTempDirectory(std::string_view prefix)
{
    if (prefix.empty()) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "sample temporary directory prefix must be non-empty");
    }

    std::error_code tempDirectoryError;
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(tempDirectoryError);
    if (tempDirectoryError) {
        Core::Error error{Core::CoreErrorCode::Io, "failed to query system temporary directory"};
        error.setNativeCode(tempDirectoryError.value());
        error.addContext("createUniqueTempDirectory", tempDirectoryError.message());
        return Core::failure(std::move(error));
    }

    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    constexpr Core::u32 MaxAttempts = 256;
    for (Core::u32 attempt = 0; attempt < MaxAttempts; ++attempt) {
        std::string directoryName{prefix};
        directoryName += '_';
        directoryName += std::to_string(seed);
        directoryName += '_';
        directoryName += std::to_string(attempt);

        const std::filesystem::path candidate = tempRoot / directoryName;
        std::error_code createError;
        if (std::filesystem::create_directory(candidate, createError)) {
            return candidate;
        }
        if (!createError) {
            continue;
        }

        std::error_code existsError;
        const bool candidateAlreadyExists = std::filesystem::exists(candidate, existsError);
        if (existsError || !candidateAlreadyExists) {
            Core::Error error{Core::CoreErrorCode::Io, "failed to create sample temporary directory"};
            error.setNativeCode(createError.value());
            error.addContext("createUniqueTempDirectory", createError.message());
            error.addContext("candidate", candidate.string());
            return Core::failure(std::move(error));
        }
    }

    Core::Error error{Core::CoreErrorCode::AlreadyExists, "failed to allocate a unique sample temporary directory"};
    error.addContext("attempts", std::to_string(MaxAttempts));
    return Core::failure(std::move(error));
}

} // namespace Tina::Sample
