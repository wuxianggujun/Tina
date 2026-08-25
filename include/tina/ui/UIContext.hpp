#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/Window.hpp>
#include <tina/ui/UIContextConfig.hpp>
#include <tina/ui/UIContextStatistics.hpp>
#include <tina/ui/UINodeId.hpp>

#include <memory>
#include <memory_resource>

namespace Tina::UI {

class IUITextRasterizer;
class UIAuthoring;
class UIElementBuildTransaction;
class UIInputRouter;
class UILayoutDebugger;
class UIMotionController;
class UIPublicationPipeline;
class UIRootBuilder;
class UIRootOwner;
class UIRoutedPointerListenerToken;
class UIStyleController;
class UITextSystem;
class UITreeUpdater;

// Single-owner-thread retained UI composition root for one WindowId. Feature
// operations are exposed by narrow capabilities; this class owns lifetime,
// capacity accounting, and the private retained state only.
class UIContext final {
  public:
    [[nodiscard]] static Core::Result<std::unique_ptr<UIContext>>
    Create(Platform::WindowId ownerWindow,
           UIContextCapacityConfig capacityConfig = {},
           std::pmr::memory_resource& resource =
               *std::pmr::get_default_resource());
    [[nodiscard]] static Core::Result<std::unique_ptr<UIContext>>
    Create(Platform::WindowId ownerWindow,
           UIContextCapacityConfig capacityConfig,
           std::unique_ptr<IUITextRasterizer> textRasterizer,
           std::pmr::memory_resource& resource =
               *std::pmr::get_default_resource());

    ~UIContext() noexcept;

    UIContext(const UIContext&) = delete;
    UIContext& operator=(const UIContext&) = delete;
    UIContext(UIContext&&) = delete;
    UIContext& operator=(UIContext&&) = delete;

    [[nodiscard]] Platform::WindowId ownerWindow() const noexcept;
    [[nodiscard]] bool contains(UINodeId node) const noexcept;

    [[nodiscard]] UIAuthoring authoring() noexcept;
    [[nodiscard]] UIStyleController style() noexcept;
    [[nodiscard]] UIMotionController motion() noexcept;
    [[nodiscard]] UITextSystem text() noexcept;
    [[nodiscard]] UIPublicationPipeline publication() noexcept;
    [[nodiscard]] UILayoutDebugger layoutDebugger() noexcept;
    [[nodiscard]] UIInputRouter input() noexcept;

    [[nodiscard]] UIContextStatistics statistics() const noexcept;
    [[nodiscard]] usize liveNodeCount() const noexcept;
    [[nodiscard]] usize liveRootCount() const noexcept;

  private:
    friend class UIAuthoring;
    friend class UIElementBuildTransaction;
    friend class UIInputRouter;
    friend class UIMotionController;
    friend class UIPublicationPipeline;
    friend class UILayoutDebugger;
    friend class UIRootBuilder;
    friend class UIRootOwner;
    friend class UIRoutedPointerListenerToken;
    friend class UIStyleController;
    friend class UITextSystem;
    friend class UITreeUpdater;

    struct Impl;

    explicit UIContext(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace Tina::UI
