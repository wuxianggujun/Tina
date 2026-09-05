add_library(tina_localization STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/localization/LocalizationCatalog.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/localization/LocalizationErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/localization/LocalizedText.hpp
    LocalizationCatalog.cpp
)
add_library(Tina::Localization ALIAS tina_localization)

target_compile_features(tina_localization PUBLIC cxx_std_23)
target_include_directories(tina_localization PUBLIC ${PROJECT_SOURCE_DIR}/include)
# Core only, like Tina::Navigation2D. The cooked-payload bridge lives on the Asset side and depends
# downward onto this module, so Tina::Asset and Tina::AssetFormat stay out of this link line.
target_link_libraries(tina_localization
    PUBLIC Tina::Core
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_localization PROPERTIES CXX_EXTENSIONS OFF)
