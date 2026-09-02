#include <tina/core/io/ContentRoot.hpp>

#include "PathUtil.hpp"

#include <tina/core/text/Utf8.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace Tina::Core {

Result<ContentRoot> ContentRoot::Create(std::string_view baseDirectoryUtf8)
try
{
    // Only the two conditions that make a base unusable as text are rejected here. Whether
    // the directory exists, is readable, or is even meaningful on this platform is not
    // checked, because a browser frontend legitimately builds a root before its preload has
    // populated anything.
    if (baseDirectoryUtf8.empty())
    {
        return Core::failure(CoreErrorCode::InvalidArgument, "content root base directory is empty");
    }
    if (!isStrictUtf8WithoutNul(baseDirectoryUtf8))
    {
        return Core::failure(CoreErrorCode::InvalidArgument,
                             "content root base directory must be strict UTF-8 with no NUL");
    }
    return ContentRoot{std::string{baseDirectoryUtf8}};
}
catch (const std::bad_alloc&)
{
    return Core::failure(CoreErrorCode::OutOfMemory, "content root allocation failed");
}

Result<std::string> ContentRoot::resolve(std::string_view relativePath) const
try
{
    // Empty root first: an empty base would otherwise join into a bare relative path, which
    // resolves against the process working directory. That is the one outcome this type
    // exists to prevent, and it fails far away from here as a missing file.
    if (m_baseDirectory.empty())
    {
        return Core::failure(CoreErrorCode::NotFound,
                             "no content root was configured for this application");
    }
    if (!Detail::isSafeRelativeContentPath(relativePath))
    {
        return Core::failure(CoreErrorCode::InvalidArgument,
                             "content relative path must be UTF-8, '/'-separated, and below the "
                             "content root");
    }
    return Detail::joinContentPath(m_baseDirectory, relativePath);
}
catch (const std::bad_alloc&)
{
    return Core::failure(CoreErrorCode::OutOfMemory, "content path allocation failed");
}

} // namespace Tina::Core
