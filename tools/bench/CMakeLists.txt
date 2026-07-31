add_executable(tina_bench
    main.cpp
    UIBenchmarkWorkloads.cpp
    UIBenchmarkWorkloads.hpp
)
target_compile_features(tina_bench PRIVATE cxx_std_23)
target_link_libraries(tina_bench
    PRIVATE
        Tina::ProjectOptions
        Tina::Core
        Tina::Runtime
        Tina::Platform
        Tina::Render
        Tina::Task
        Tina::UI
        Tina::UIRenderIntegration
)
set_target_properties(tina_bench PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME tina_bench
)
