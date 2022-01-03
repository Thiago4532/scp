cmake_minimum_required(VERSION 3.12)

# Set the default CMAKE_BUILD_TYPE to Release.
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()

project(scp C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_EXTENSIONS ON)

set(SOURCES
    src/main.c
)
add_subdirectory(lib/stb)

add_executable(scp ${SOURCES})
# target_include_directories(scp
#     PRIVATE
#         ${PROJECT_SOURCE_DIR}/lib/stb
# )
target_link_libraries(scp
    X11
    Xext
    m
    stb
)
