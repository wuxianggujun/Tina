add_executable(tina_sample_null
    main.cpp
)

target_compile_features(tina_sample_null PRIVATE cxx_std_23)
target_link_libraries(tina_sample_null
    PRIVATE
        Tina::ProjectOptions
        Tina::Runtime
        Tina::Platform
        Tina::Task
        Tina::Render
)
set_target_properties(tina_sample_null PROPERTIES
    CXX_EXTENSIONS OFF
)
