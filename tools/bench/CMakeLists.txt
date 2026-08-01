set(TINA_BENCH_WORKLOAD_SOURCES
    UIBenchmarkWorkloads.cpp
    UIBenchmarkWorkloads.hpp
)

set(TINA_BENCH_LINK_LIBRARIES
    Tina::ProjectOptions
    Tina::Core
    Tina::Runtime
    Tina::Platform
    Tina::Render
    Tina::Task
    Tina::UI
    Tina::UIRenderIntegration
)

add_library(tina_bench_workloads OBJECT ${TINA_BENCH_WORKLOAD_SOURCES})
target_compile_features(tina_bench_workloads PRIVATE cxx_std_23)
target_link_libraries(tina_bench_workloads PRIVATE ${TINA_BENCH_LINK_LIBRARIES})
set_target_properties(tina_bench_workloads PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_bench
    main.cpp
    $<TARGET_OBJECTS:tina_bench_workloads>
)
target_compile_features(tina_bench PRIVATE cxx_std_23)
target_link_libraries(tina_bench
    PRIVATE
        ${TINA_BENCH_LINK_LIBRARIES}
)
set_target_properties(tina_bench PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME tina_bench
)

if(TINA_BUILD_TESTING)
    add_executable(tina_bench_tests
        UIBenchmarkWorkloadsTests.cpp
        $<TARGET_OBJECTS:tina_bench_workloads>
    )
    target_compile_features(tina_bench_tests PRIVATE cxx_std_23)
    target_link_libraries(tina_bench_tests
        PRIVATE
            ${TINA_BENCH_LINK_LIBRARIES}
            GTest::gtest_main
    )
    set_target_properties(tina_bench_tests PROPERTIES CXX_EXTENSIONS OFF)
endif()
