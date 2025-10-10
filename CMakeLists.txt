cmake_minimum_required(VERSION 3.15)
project(sokol_shader_watcher LANGUAGES C)
add_executable(${PROJECT_NAME} main.c)
target_include_directories(${PROJECT_NAME} PRIVATE modules/xhl/include)