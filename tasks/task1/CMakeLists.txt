cmake_minimum_required(VERSION 3.18)

get_filename_component(target_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)

add_executable("${target_name}_client" client.cpp)
add_executable("${target_name}_server" server.cpp)

