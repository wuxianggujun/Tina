add_executable(tina_physics2d_bench
    main.cpp
)

target_compile_features(tina_physics2d_bench PRIVATE cxx_std_23)
target_link_libraries(tina_physics2d_bench
    PRIVATE
        Tina::ProjectOptions
        Tina::Physics2D
)
set_target_properties(tina_physics2d_bench PROPERTIES
    CXX_EXTENSIONS OFF
)
