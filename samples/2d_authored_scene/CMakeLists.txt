add_executable(tina_sample_2d_authored_scene
    main.cpp
)

target_compile_features(tina_sample_2d_authored_scene PRIVATE cxx_std_23)
target_link_libraries(tina_sample_2d_authored_scene
    PRIVATE
        Tina::ProjectOptions
        Tina::Runtime
        Tina::Gameplay2D
        Tina::Asset
        Tina::Audio
        Tina::Scene
        Tina::Platform
        Tina::Task
        Tina::Render
)
set_target_properties(tina_sample_2d_authored_scene PROPERTIES CXX_EXTENSIONS OFF)
