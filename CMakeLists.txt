cmake_minimum_required(VERSION 3.10)
project(chip8 C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_C_STANDARD 99)

file(COPY font/Inconsolata-Bold.ttf DESTINATION .)

add_executable(chip8-eml src/main.c src/chip8.c)
target_link_libraries(chip8-eml SDL2 SDL2_ttf)
