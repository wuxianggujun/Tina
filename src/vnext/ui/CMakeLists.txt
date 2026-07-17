set(TINA_UI_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UI.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIDirty.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedHit.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedLayout.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedStructure.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIContext.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIHitTest.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UILayout.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/InputRouting.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UINodeId.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIWidgetKind.hpp
)

add_library(tina_ui STATIC
    ${TINA_UI_PUBLIC_HEADERS}
    UIContext.cpp
)
add_library(Tina::UI ALIAS tina_ui)

target_compile_features(tina_ui PUBLIC cxx_std_23)
target_include_directories(tina_ui PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_ui
    PUBLIC
        Tina::Core
        Tina::Platform
    PRIVATE
        Tina::ProjectOptions
)
set_target_properties(tina_ui PROPERTIES CXX_EXTENSIONS OFF)
