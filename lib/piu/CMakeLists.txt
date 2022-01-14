cmake_minimum_required(VERSION 3.12)

# Determine if piu is built as a subproject (using add_subdirectory)
# or if it is the master project.
if (NOT DEFINED PIU_MASTER_PROJECT)
  set(PIU_MASTER_PROJECT OFF)
  if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PIU_MASTER_PROJECT ON)
  endif ()
endif ()

# Set the default CMAKE_BUILD_TYPE to Release.
if (PIU_MASTER_PROJECT AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()
project(piu C)

if (PIU_MASTER_PROJECT AND NOT CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif ()

set(PIU_SOURCES
  src/internal/PIUBuff.c
  src/internal/PIUPacket.c
  src/PIUSocket.c
)

option(PIU_TEST "Generate the test target." ${PIU_MASTER_PROJECT})

if (PIU_MASTER_PROJECT)
  message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()

if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif ()

# Add the library
add_library(piu ${PIU_SOURCES})
target_include_directories(piu PUBLIC include/)
target_link_libraries(piu
  pthread
)

if (PIU_TEST)
  add_subdirectory(test)
endif ()

# vim: et ts=8 sts=2 sw=2
