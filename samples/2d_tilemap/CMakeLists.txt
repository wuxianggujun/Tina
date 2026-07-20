add_executable(tina_sample_2d_tilemap
    main.cpp
)

target_compile_features(tina_sample_2d_tilemap PRIVATE cxx_std_23)
target_link_libraries(tina_sample_2d_tilemap
    PRIVATE
        Tina::ProjectOptions
        Tina::Runtime
        Tina::Asset
        Tina::Platform
        Tina::Task
        Tina::Render
)
set_target_properties(tina_sample_2d_tilemap PROPERTIES CXX_EXTENSIONS OFF)
