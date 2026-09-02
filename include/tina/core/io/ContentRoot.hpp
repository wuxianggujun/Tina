#pragma once

#include <tina/core/error/Result.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace Tina::Core {

// Where a product's shipped, read-only content lives, as a UTF-8 base directory plus
// validated joins below it.
//
// This exists because locating files is the only genuinely platform-specific part of
// loading content, and until now nothing owned that decision. Each content site invented
// its own policy, and the engine's own samples reached four mutually incompatible answers:
// a scratch directory under temp_directory_path, a --catalog= command-line argument, a path
// handed down by the caller, and applicationFilePath. All four are desktop-only, so content
// written against any of them cannot be ported, and nothing reports that until someone
// tries. Reading itself was never the problem: the runtime read path already funnels
// through Core::readFile at three call sites.
//
// A ContentRoot is a value, not a service. The frontend builds one -- it is the only code
// that knows what the platform offers -- stores it in EngineConfig, and content reaches it
// through the EngineConfig it already receives on every phase context. Content therefore
// names what it wants ("content/manifest.tmnft") and never spells out where that is.
//
// Nothing here touches the filesystem. A resolved path may not exist, and existence stays
// the caller's to check, matching ApplicationPaths. This also means construction cannot
// fail on a read-only or not-yet-populated location, which is what makes it usable on a
// browser before a preload completes.
//
// Kept distinct from userApplicationDirectory in UserPaths.hpp, which answers where a
// product may *write*. Conflating the two would put save games in a read-only APK.
class ContentRoot final {
  public:
    // An empty root, which resolves nothing. This is the default in EngineConfig because a
    // game with no shipped content is legitimate -- most tests and several samples have
    // none -- so an empty root has to be legal to construct while still failing loudly the
    // moment something asks it for a file.
    ContentRoot() = default;

    // baseDirectory is an absolute or relative UTF-8 directory path, without a trailing
    // separator. It is stored verbatim: this type validates what callers ask *of* a root,
    // not the root itself, because only the frontend can know whether a given base is
    // meaningful on its platform. Returns InvalidArgument for an empty path or one holding
    // an embedded NUL, both of which would otherwise surface far away as a failed open.
    [[nodiscard]] static Result<ContentRoot> Create(std::string_view baseDirectoryUtf8);

    [[nodiscard]] bool empty() const noexcept { return m_baseDirectory.empty(); }
    [[nodiscard]] std::string_view baseDirectory() const noexcept { return m_baseDirectory; }

    // Joins relativePath below the base and returns a UTF-8 path suitable for
    // Core::readFile.
    //
    // relativePath may name nested directories and must use '/' on every platform. It is
    // joined verbatim rather than sanitized, so anything that could resolve outside the
    // root is rejected: an absolute path, a '\' separator, a '.' or '..' component, an
    // empty component, and an embedded NUL all return InvalidArgument. These are the same
    // rules as Core::applicationFilePath, deliberately -- a caller moving from one to the
    // other should not have to relearn what is accepted.
    //
    // Returns NotFound on an empty root. Not InvalidArgument: the argument is fine, the
    // product simply was not given a place to load from, and telling those apart is the
    // difference between fixing a call site and fixing frontend startup.
    [[nodiscard]] Result<std::string> resolve(std::string_view relativePath) const;

  private:
    explicit ContentRoot(std::string baseDirectory) noexcept
        : m_baseDirectory(std::move(baseDirectory))
    {
    }

    std::string m_baseDirectory;
};

} // namespace Tina::Core
