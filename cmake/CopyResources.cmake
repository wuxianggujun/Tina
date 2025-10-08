##
# CopyResources.cmake
# 递归拷贝 SRC 下的所有资源到 DST，排除任意路径片段为 "shaders" 的文件
# 用法：cmake -DSRC=... -DDST=... -P CopyResources.cmake

if(NOT DEFINED SRC)
  message(FATAL_ERROR "CopyResources.cmake: 需要定义 SRC")
endif()
if(NOT DEFINED DST)
  message(FATAL_ERROR "CopyResources.cmake: 需要定义 DST")
endif()

file(MAKE_DIRECTORY "${DST}")

# 仅匹配文件（不包含目录），保持相对路径，支持 Windows 与 POSIX 路径分隔符
file(GLOB_RECURSE RES_FILES RELATIVE "${SRC}" LIST_DIRECTORIES false "${SRC}/*")

foreach(rel IN LISTS RES_FILES)
  # 若相对路径中包含 /shaders/ 或 \shaders\，则跳过
  if(rel MATCHES "[/\\\\]shaders[/\\\\]")
    continue()
  endif()

  set(src_file "${SRC}/${rel}")
  get_filename_component(rel_dir "${rel}" DIRECTORY)
  if(rel_dir)
    file(MAKE_DIRECTORY "${DST}/${rel_dir}")
  endif()
  file(COPY "${src_file}" DESTINATION "${DST}/${rel_dir}")
endforeach()

