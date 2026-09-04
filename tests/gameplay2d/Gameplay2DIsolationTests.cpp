#include <gtest/gtest.h>

TEST(Gameplay2DIsolation, HeaderCompilesWithoutPhysicsDefine)
{
    // The real assertion is that header_isolation/Scene2DRuntimeHeader.cpp
    // compiles Scene2DRuntime.hpp without TINA_HAS_PHYSICS2D; this target only
    // depends on that object so building it is required to build this test.
    SUCCEED();
}
