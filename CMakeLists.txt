cmake_minimum_required(VERSION 3.20)
project(MyEngine LANGUAGES C CXX)

add_executable(MyEngine
    src/main.cpp
    glad/src/glad.c
)

target_include_directories(MyEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/glad/include
    ${CMAKE_SOURCE_DIR}          # <-- lets main.cpp find stb_image.h at root
)

add_subdirectory(external/glfw)
target_link_libraries(MyEngine PRIVATE glfw)

set_target_properties(MyEngine PROPERTIES
    C_STANDARD 17
    CXX_STANDARD 17
)