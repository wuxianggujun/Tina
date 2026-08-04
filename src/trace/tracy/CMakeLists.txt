find_package(Tracy 0.13.1 CONFIG REQUIRED)

add_library(tina_trace_tracy STATIC
    TraceTracy.cpp
)
add_library(Tina::TraceTracy ALIAS tina_trace_tracy)

target_compile_features(tina_trace_tracy PUBLIC cxx_std_23)
target_include_directories(tina_trace_tracy
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_trace_tracy
    PRIVATE
        "$<BUILD_INTERFACE:Tina::ProjectOptions>"
        Tracy::TracyClient
)
set_target_properties(tina_trace_tracy PROPERTIES CXX_EXTENSIONS OFF)
