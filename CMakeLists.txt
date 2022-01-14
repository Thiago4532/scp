cmake_minimum_required(VERSION 3.12)

# Set the default CMAKE_BUILD_TYPE to Release.
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()

project(scp C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_EXTENSIONS ON)

set(SCP_SOURCES
    src/scp.c
)
set(PLAYER_SOURCES
    src/player.c
)
add_subdirectory(lib/stb)
add_subdirectory(lib/piu)

add_executable(scp ${SCP_SOURCES})
target_link_libraries(scp
    X11
    Xext
    m
    stb
    avcodec
    avutil
    piu
)

add_executable(player ${PLAYER_SOURCES})
target_link_libraries(player
    SDL2
    SDL2_image
    m
    stb
    avcodec
    avutil
    piu
)
