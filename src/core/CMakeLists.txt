add_library(tina_core STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/Compiler.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/EnumFlags.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/Platform.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/ScopeExit.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/SourceLocation.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/Types.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/Assert.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/error/Error.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/error/Result.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/id/GenerationId.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/id/GenerationPool.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/memory/CountingMemoryResource.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/memory/FrameArena.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/memory/MemoryStatistics.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/memory/MemoryTag.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/memory/MemoryTracker.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/time/FixedStepAccumulator.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/time/MonotonicClock.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/text/Utf8.hpp
    diagnostics/Assert.cpp
    diagnostics/Assert.hpp
    error/Error.hpp
    error/Result.hpp
    id/GenerationId.cpp
    id/GenerationId.hpp
    id/GenerationPool.hpp
    memory/CountingMemoryResource.cpp
    memory/FrameArena.cpp
    memory/MemoryTracker.cpp
    base/Compiler.hpp
    base/EnumFlags.hpp
    base/Platform.hpp
    base/ScopeExit.hpp
    base/SourceLocation.hpp
    base/Types.hpp
    time/Clock.cpp
    time/Clock.hpp
    time/FixedStepAccumulator.cpp
    time/FrameTimer.hpp
)
add_library(Tina::Core ALIAS tina_core)

target_compile_features(tina_core PUBLIC cxx_std_23)
target_include_directories(tina_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(tina_core PRIVATE Tina::ProjectOptions)
target_compile_definitions(tina_core PRIVATE TINA_CORE_BUILDING)
set_target_properties(tina_core PROPERTIES CXX_EXTENSIONS OFF)

if(TINA_BUILD_LEGACY)
    # Compatibility code that still depends on EASTL and xxHash is intentionally separate from the
    # zero-external-dependency C++23 Core. It will shrink as callers migrate to focused Core APIs.
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

    target_compile_features(tina_core_legacy PUBLIC cxx_std_23)
    target_include_directories(tina_core_legacy PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(tina_core_legacy
        PUBLIC
            Tina::Core
            EASTL
            xxHash::xxhash
    )
    target_link_libraries(tina_core_legacy PRIVATE Tina::ProjectOptions)
    set_target_properties(tina_core_legacy PROPERTIES CXX_EXTENSIONS OFF)

    # Game-specific generation algorithms remain with the Legacy product until Scene/2D migration.
    add_library(tina_procedural STATIC
        Map2DNoise.cpp
        Map2DNoise.hpp
        Noise.cpp
        Noise.hpp
    )
    add_library(Tina::Procedural ALIAS tina_procedural)

    target_compile_features(tina_procedural PUBLIC cxx_std_23)
    target_include_directories(tina_procedural PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(tina_procedural PUBLIC Tina::Core)
    target_link_libraries(tina_procedural PRIVATE Tina::ProjectOptions)
    set_target_properties(tina_procedural PROPERTIES CXX_EXTENSIONS OFF)
endif()
