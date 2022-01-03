cmake_minimum_required(VERSION 3.12)

project(stb C)
set(STB_SOURCES
    src/stb_image_write.c
)

add_library(stb ${STB_SOURCES})
target_include_directories(stb PUBLIC stb-git/)
