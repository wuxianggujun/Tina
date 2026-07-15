add_library(tina_core STATIC
    diagnostics/Assert.cpp
    diagnostics/Assert.hpp
    error/Error.hpp
    error/Result.hpp
    base/Compiler.hpp
    base/EnumFlags.hpp
    base/Platform.hpp
    base/ScopeExit.hpp
    base/SourceLocation.hpp
    base/Types.hpp
    time/Clock.cpp
    time/Clock.hpp
    time/FrameTimer.hpp
)
add_library(Tina::Core ALIAS tina_core)

target_compile_features(tina_core PUBLIC cxx_std_20)
target_include_directories(tina_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(tina_core PRIVATE Tina::ProjectOptions)
set_target_properties(tina_core PROPERTIES CXX_EXTENSIONS OFF)

# Compatibility code that still depends on EASTL and xxHash is intentionally separate from the
# zero-external-dependency C++20 Core. It will shrink as callers migrate to focused Core APIs.
add_library(tina_core_legacy STATIC
    Core.hpp
    EASTLAlloc.cpp
    Hash.hpp
    Memory.hpp
    Path.cpp
    Path.hpp
    Time.hpp
)
add_library(Tina::CoreLegacy ALIAS tina_core_legacy)

target_compile_features(tina_core_legacy PUBLIC cxx_std_20)
target_include_directories(tina_core_legacy PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(tina_core_legacy
    PUBLIC
        Tina::Core
        EASTL
        xxHash::xxhash
)
target_link_libraries(tina_core_legacy PRIVATE Tina::ProjectOptions)
set_target_properties(tina_core_legacy PROPERTIES CXX_EXTENSIONS OFF)

# Game-specific generation algorithms are not part of the low-level Core contract.
add_library(tina_procedural STATIC
    Map2DNoise.cpp
    Map2DNoise.hpp
    Noise.cpp
    Noise.hpp
)
add_library(Tina::Procedural ALIAS tina_procedural)

target_compile_features(tina_procedural PUBLIC cxx_std_20)
target_include_directories(tina_procedural PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(tina_procedural PUBLIC Tina::Core)
target_link_libraries(tina_procedural PRIVATE Tina::ProjectOptions)
set_target_properties(tina_procedural PROPERTIES CXX_EXTENSIONS OFF)
