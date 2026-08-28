add_executable(tina_sample_network
    main.cpp
)

target_compile_features(tina_sample_network PRIVATE cxx_std_23)
target_link_libraries(tina_sample_network
    PRIVATE
        Tina::ProjectOptions
        Tina::Network
        Tina::Task
        Tina::Core
)
set_target_properties(tina_sample_network PROPERTIES CXX_EXTENSIONS OFF)
