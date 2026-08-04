add_library(tina_core STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/Compiler.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/EnumFlags.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/Platform.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/ScopeExit.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/SourceLocation.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/Types.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/Assert.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/DiagnosticChannel.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/Diagnostics.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/LogLevel.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/LogRecord.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/error/Error.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/error/Result.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/hash/ContentHash.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/hash/ContentHashDigest.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/io/ReadFile.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/io/WriteFile.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/id/AssetId.hpp

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
    ${PROJECT_SOURCE_DIR}/include/tina/core/trace/Trace.hpp
    diagnostics/Assert.cpp
    diagnostics/Diagnostics.cpp
    hash/ContentHashDigest.cpp
    io/ReadFile.cpp
    io/Utf8Path.hpp
    io/WriteFile.cpp
    id/GenerationId.cpp
    memory/CountingMemoryResource.cpp
    memory/FrameArena.cpp
    memory/MemoryTracker.cpp
    time/FixedStepAccumulator.cpp
    time/MonotonicClock.cpp
)
add_library(Tina::Core ALIAS tina_core)

target_compile_features(tina_core PUBLIC cxx_std_23)
target_include_directories(tina_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(tina_core
    PRIVATE
        "$<BUILD_INTERFACE:Tina::ProjectOptions>"
        xxHash::xxhash
)
if (TINA_TRACE_BACKEND STREQUAL "tracy")
    target_link_libraries(tina_core PUBLIC Tina::TraceTracy)
    target_compile_definitions(tina_core
        PUBLIC
            TINA_TRACE_BACKEND_ENABLED=1
    )
else ()
    target_compile_definitions(tina_core PUBLIC TINA_TRACE_BACKEND_NONE=1)
endif ()
target_compile_definitions(tina_core PRIVATE TINA_CORE_BUILDING)
set_target_properties(tina_core PROPERTIES CXX_EXTENSIONS OFF)
