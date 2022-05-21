cmake_minimum_required(VERSION 3.20)

file(GLOB task_folders ${CMAKE_CURRENT_LIST_DIR}/*)

foreach(task_folder ${task_folders})
    if(IS_DIRECTORY ${task_folder})
        add_subdirectory(${task_folder})
    endif()
endforeach()
