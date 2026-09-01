set(TINA_SAVE_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/save/SaveErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/save/SaveMigration.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/save/SaveStore.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/save/SaveTypes.hpp
)

add_library(tina_save STATIC
    ${TINA_SAVE_PUBLIC_HEADERS}
    SaveFormat.cpp
    SaveFormat.hpp
    SaveMigration.cpp
    SaveStore.cpp
)
add_library(Tina::Save ALIAS tina_save)

target_compile_features(tina_save PUBLIC cxx_std_23)
target_include_directories(tina_save
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(tina_save
    PUBLIC Tina::Core Tina::Task
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_save PROPERTIES CXX_EXTENSIONS OFF)
