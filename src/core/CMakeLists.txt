add_library(tina_core STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/CancellationSignal.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/Compiler.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/EnumFlags.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/MoveOnlyFunction.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/Platform.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/ScopeExit.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/SourceLocation.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/base/Types.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/Assert.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/CrashHandler.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/DiagnosticChannel.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/Diagnostics.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/Log.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/LogFormat.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/LogLevel.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/diagnostics/LogRecord.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/error/Error.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/error/Result.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/hash/ContentHash.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/hash/ContentHashDigest.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/io/ApplicationPaths.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/io/ContentRoot.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/io/ReadFile.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/io/UserPaths.hpp
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
    ${PROJECT_SOURCE_DIR}/include/tina/core/text/JsonWriter.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/text/ParseFloat.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/text/Utf8.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/core/trace/Trace.hpp
    diagnostics/Assert.cpp
    diagnostics/CrashHandler.cpp
    diagnostics/Diagnostics.cpp
    diagnostics/LogFormat.cpp
    diagnostics/LogRecord.cpp
    hash/ContentHashDigest.cpp
    io/ApplicationPaths.cpp
    io/ContentRoot.cpp
    io/PathUtil.cpp
    io/PathUtil.hpp
    io/ReadFile.cpp
    io/UserPaths.cpp
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
if (WIN32)
    # CrashHandler resolves backtrace symbols through DbgHelp.
    target_link_libraries(tina_core PRIVATE dbghelp)
endif ()
if (ANDROID)
    # Diagnostics' platform sink writes to logcat through __android_log_write.
    target_link_libraries(tina_core PRIVATE log)
endif ()
if (TINA_TRACE_BACKEND STREQUAL "tracy")
    target_link_libraries(tina_core PUBLIC Tina::TraceTracy)
    target_compile_definitions(tina_core
        PUBLIC
            TINA_TRACE_BACKEND_ENABLED=1
    )
else ()
    target_compile_definitions(tina_core PUBLIC TINA_TRACE_BACKEND_NONE=1)
endif ()
# Compile-time log floor, matching LogLevel's numeric values. Levels below it
# are stripped from the build entirely. Debug keeps Trace so a developer can see
# everything; other configurations start at Info to keep hot paths clean.
if (NOT DEFINED TINA_LOG_LEVEL_COMPILED)
    set(TINA_LOG_LEVEL_COMPILED "$<IF:$<CONFIG:Debug>,0,2>")
endif ()
target_compile_definitions(tina_core
    PUBLIC
        TINA_LOG_LEVEL_COMPILED=${TINA_LOG_LEVEL_COMPILED}
)
target_compile_definitions(tina_core PRIVATE TINA_CORE_BUILDING)
set_target_properties(tina_core PROPERTIES CXX_EXTENSIONS OFF)
