#include <utility>

#include "tina/layer/Layer.hpp"

namespace Tina {

Layer::Layer(std::string  name)
    : m_debugName(std::move(name)) {
}

} // namespace Tina
