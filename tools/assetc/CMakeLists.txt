add_executable(tina_assetc
    main.cpp
)
target_compile_features(tina_assetc PRIVATE cxx_std_23)
target_link_libraries(tina_assetc
    PRIVATE
        Tina::ProjectOptions
        Tina::Asset
)
set_target_properties(tina_assetc PROPERTIES CXX_EXTENSIONS OFF)
