cmake_minimum_required(VERSION 3.18)

get_filename_component(target_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)


add_library("${target_name}_common" common/common.cpp)
target_link_libraries("${target_name}_common" enet spdlog function2)

add_library("${target_name}_game" game/Entity.cpp)
target_link_libraries("${target_name}_game" spdlog)


add_executable("${target_name}_client" client.cpp)
target_link_libraries("${target_name}_client"
    "${target_name}_common" "${target_name}_game" allegro allegro_font allegro_primitives)

add_executable("${target_name}_server" server.cpp)
target_link_libraries("${target_name}_server" "${target_name}_common" "${target_name}_game")

add_executable("${target_name}_lobby" lobby.cpp)
target_link_libraries("${target_name}_lobby" "${target_name}_common")
