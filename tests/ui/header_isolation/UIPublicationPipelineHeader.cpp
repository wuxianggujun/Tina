#include <tina/ui/UIPublicationPipeline.hpp>

#include <type_traits>
#include <utility>

using LayoutViewResult = decltype(
    std::declval<const Tina::UI::UIPublicationPipeline&>().committedLayout());
static_assert(std::is_same_v<LayoutViewResult, Tina::UI::UICommittedLayoutView>);
