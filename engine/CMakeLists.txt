file(GLOB_RECURSE LOG_SOURCES CONFIGURE_DEPENDS *.c** *.h**)
message(STATUS "头文件资源" ${LOG_SOURCES})
list(APPEND ${ENGINE_SOURCES} ${LOG_SOURCES})