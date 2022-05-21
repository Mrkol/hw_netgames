cmake_minimum_required(VERSION 3.20)

get_filename_component(target_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)


add_library("${target_name}_common" common/common.cpp)
target_link_libraries("${target_name}_common" enet spdlog function2)

add_library("${target_name}_game" game/Entity.cpp)
target_link_libraries("${target_name}_game" PUBLIC spdlog glm::glm)


add_executable("${target_name}_client" client.cpp)
target_link_libraries("${target_name}_client"
    "${target_name}_common" "${target_name}_game" allegro allegro_font allegro_primitives)

add_executable("${target_name}_server" server.cpp)
target_link_libraries("${target_name}_server" "${target_name}_common" "${target_name}_game")

add_executable("${target_name}_lobby" lobby.cpp)
target_link_libraries("${target_name}_lobby" "${target_name}_common")

if (WIN32)
  string(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_TOLOWER)
  foreach(ADDON "" "_font" "_primitives")
    set(LIBNAME "allegro${ADDON}")
    if(CMAKE_BUILD_TYPE_TOLOWER STREQUAL "debug")
      set(${LIBNAME} "${${LIBNAME}}-debug")
    endif()
    add_custom_command(TARGET client
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}\\_deps\\allegro-build\\${LIBNAME}-5.2.dll"
            "${CMAKE_BINARY_DIR}\\${LIBNAME}-5.2.dll"
        )
  endforeach()
endif()
