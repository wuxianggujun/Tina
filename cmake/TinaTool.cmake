function(Group_Files_ByDirectory FILE_LIST)
    foreach(file IN LISTS ${FILE_LIST})
    get_filename_component(file_Path "${file}" DIRECTORY)
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/src" "" group_path "${file_path}")
    string(REPLACE "/" "\\" group_name "${group_path}")
    source_group("${group_name}" FILES "${file}")
    endforeach
endfunction
