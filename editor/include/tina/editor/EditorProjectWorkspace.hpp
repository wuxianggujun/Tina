#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <string>
#include <string_view>

namespace Tina::Editor {

struct EditorProjectWorkspaceDesc final {
    std::string_view projectRootUtf8{};
    // Root of editable authoring sources consumed by the Cooker.
    std::string_view sourceRootUtf8{};
    // Root of the immutable Cooked Catalog consumed by preview Runtime services.
    std::string_view cookedCatalogRootUtf8{};
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::WindowsX64;
};

struct EditorProjectWorkspaceConfig final {
    Core::usize rootPathByteCapacity = 4096;
};

// Immutable, owning roots for one current Editor project. Create performs only
// lexical validation so a project may be described before its directories are
// created. File operations must still enforce final-path containment when they
// follow symlinks or reparse points. Returned views remain stable until move or
// destruction of the workspace.
class EditorProjectWorkspace final {
public:
    EditorProjectWorkspace(const EditorProjectWorkspace&) = delete;
    EditorProjectWorkspace& operator=(const EditorProjectWorkspace&) = delete;
    EditorProjectWorkspace(EditorProjectWorkspace&&) noexcept = default;
    EditorProjectWorkspace& operator=(EditorProjectWorkspace&&) noexcept = default;

    [[nodiscard]] static Core::Result<EditorProjectWorkspace>
    Create(EditorProjectWorkspaceDesc desc, EditorProjectWorkspaceConfig config = {});

    [[nodiscard]] const EditorProjectWorkspaceConfig& config() const noexcept
    {
        return m_config;
    }
    [[nodiscard]] std::string_view projectRootUtf8() const noexcept
    {
        return m_projectRootUtf8;
    }
    [[nodiscard]] std::string_view sourceRootUtf8() const noexcept
    {
        return m_sourceRootUtf8;
    }
    [[nodiscard]] std::string_view cookedCatalogRootUtf8() const noexcept
    {
        return m_cookedCatalogRootUtf8;
    }
    [[nodiscard]] AssetFormat::TargetPlatform targetPlatform() const noexcept
    {
        return m_targetPlatform;
    }

private:
    EditorProjectWorkspace(EditorProjectWorkspaceConfig config,
                           std::string projectRootUtf8,
                           std::string sourceRootUtf8,
                           std::string cookedCatalogRootUtf8,
                           AssetFormat::TargetPlatform targetPlatform) noexcept;

    EditorProjectWorkspaceConfig m_config{};
    std::string m_projectRootUtf8{};
    std::string m_sourceRootUtf8{};
    std::string m_cookedCatalogRootUtf8{};
    AssetFormat::TargetPlatform m_targetPlatform = AssetFormat::TargetPlatform::Invalid;
};

} // namespace Tina::Editor
