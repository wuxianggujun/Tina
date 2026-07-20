add_executable(tina_sample_asset
    main.cpp
)

target_compile_features(tina_sample_asset PRIVATE cxx_std_23)
target_link_libraries(tina_sample_asset
    PRIVATE
        Tina::ProjectOptions
        Tina::Asset
        Tina::Task
        Tina::Render
)
set_target_properties(tina_sample_asset PROPERTIES
    CXX_EXTENSIONS OFF
)
