add_executable(tina_sample_2d_infrastructure
    main.cpp
)

target_compile_features(tina_sample_2d_infrastructure PRIVATE cxx_std_23)
target_link_libraries(tina_sample_2d_infrastructure
    PRIVATE
        Tina::ProjectOptions
        Tina::Runtime
        Tina::Scene
        Tina::Platform
        Tina::Task
        Tina::Render
)
set_target_properties(tina_sample_2d_infrastructure PROPERTIES CXX_EXTENSIONS OFF)
