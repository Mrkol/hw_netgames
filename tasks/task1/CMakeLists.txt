cmake_minimum_required(VERSION 3.20)

get_filename_component(target_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)


if (NOT MSVC)
	add_executable("${target_name}_client" client.cpp)
	add_executable("${target_name}_server" server.cpp)
endif ()
