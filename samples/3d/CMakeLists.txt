add_executable(tina_sample_3d_extraction
    main.cpp
)

target_compile_features(tina_sample_3d_extraction PRIVATE cxx_std_23)
target_link_libraries(tina_sample_3d_extraction
    PRIVATE
        Tina::ProjectOptions
        Tina::Core
        Tina::Runtime
        Tina::Scene
        Tina::Platform
        Tina::Task
        Tina::Render
)
set_target_properties(tina_sample_3d_extraction PROPERTIES CXX_EXTENSIONS OFF)
