cmake_minimum_required(VERSION 3.18)

project(netgames)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif()

if(NOT DEFINED CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
  set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "No dev warnings")
endif()

set(BUILD_SHARED_LIBS off)


set(CPM_DOWNLOAD_VERSION 0.34.0)

if(CPM_SOURCE_CACHE)
  set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
  message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
  file(DOWNLOAD
       https://github.com/TheLartians/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
       ${CPM_DOWNLOAD_LOCATION}
    )
endif()

include(${CPM_DOWNLOAD_LOCATION})

set(CPM_USE_LOCAL_PACKAGES True)



set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "-Werror -Wall -Wextra")

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -v")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")
endif()




CPMAddPackage(
  NAME enet
  GITHUB_REPOSITORY lsalzman/enet
  GIT_TAG v1.3.17
  )

CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    VERSION 1.9.2
  )

CPMAddPackage(
    NAME function2
    GITHUB_REPOSITORY Naios/function2
    GIT_TAG 4.2.0
  )

CPMAddPackage(
    NAME allegro
    GITHUB_REPOSITORY liballeg/allegro5
    GIT_TAG 5.2.7.0
    OPTIONS
      # Ah, yes, I want my build process to take 10 minutes by default, thank you!
      "WANT_DOCS off"
      "WANT_EXAMPLES off"
      "WANT_DEMO off"
      "WANT_TESTS off"
      "WANT_VIDEO off"
      "WANT_PHYSFS off"
      "WANT_IMAGE_WEBP off"
      "WANT_AUDIO off"
  )

if(allegro_ADDED)
  # allegro's authors have no idea how to write proper cmake files
  target_include_directories(allegro INTERFACE
      "${allegro_SOURCE_DIR}/include"
      "${allegro_BINARY_DIR}/include"
    )

  foreach(ADDON font image color primitives)
    target_include_directories("allegro_${ADDON}" INTERFACE
        "${allegro_SOURCE_DIR}/addons/${ADDON}"
      )
  endforeach()
endif()


add_subdirectory("tasks")

