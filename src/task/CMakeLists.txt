add_library(tina_task STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/task/TaskErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/task/TaskSystem.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/task/TaskGroup.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/task/bounded/BoundedTaskSystemFactory.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/task/disabled/DisabledTaskSystemFactory.hpp
    TaskGroup.cpp
    bounded/BoundedTaskSystem.cpp
    disabled/DisabledTaskSystem.cpp
)
add_library(Tina::Task ALIAS tina_task)

target_compile_features(tina_task PUBLIC cxx_std_23)
target_include_directories(tina_task PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_task PUBLIC Tina::Core)
target_link_libraries(tina_task PRIVATE Tina::ProjectOptions)
set_target_properties(tina_task PROPERTIES CXX_EXTENSIONS OFF)
