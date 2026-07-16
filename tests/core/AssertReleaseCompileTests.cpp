#include <tina/core/diagnostics/Assert.hpp>

namespace {

struct BitFieldValue {
    unsigned enabled : 1;
};

// This translation unit is compiled with NDEBUG even in the Debug test target. A disabled
// assertion must not instantiate or evaluate its expression (sizeof(bit-field) is ill-formed).
[[maybe_unused]] void compileDisabledAssertion(BitFieldValue value)
{
    TINA_ASSERT(value.enabled);
}

} // namespace
