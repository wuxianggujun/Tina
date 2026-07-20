add_executable(tina_catalog_validate
    main.cpp
)
target_compile_features(tina_catalog_validate PRIVATE cxx_std_23)
target_link_libraries(tina_catalog_validate
    PRIVATE
        Tina::ProjectOptions
        Tina::Asset
)
set_target_properties(tina_catalog_validate PROPERTIES CXX_EXTENSIONS OFF)
