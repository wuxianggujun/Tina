# 这一行代码设置 TINA_RESOURCES_PATH 宏的值为 ${CMAKE_SOURCE_DIR}/resources/，即项目根目录下的 resources 文件夹。这个设置对于在开发环境中访问资源文件非常有用。在这种情况下，宏的值是一个相对路径，可以根据项目在文件系统中的位置进行调整。
target_compile_definitions(${PROJECT_NAME} PUBLIC TINA_RESOURCES_PATH="${CMAKE_SOURCE_DIR}/resources/")
# 如果取消注释并注释掉上面的行，那么 TINA_RESOURCES_PATH 宏的值将被设置为 ./resources/，即相对于可执行文件的当前工作目录的 resources 文件夹。这个设置在发布版本中可能更有用，因为它使得资源文件可以与可执行文件放在同一目录下，便于分发。
target_compile_definitions(${PROJECT_NAME} PUBLIC TINA_RESOURCES_PATH="./resources/")