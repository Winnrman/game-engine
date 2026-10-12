cmake_minimum_required(VERSION 3.20)
project(MyEngine LANGUAGES C CXX)

# Default to an optimized build (world meshing is ~10x faster than Debug)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_executable(MyEngine
    src/main.cpp
    glad/src/glad.c
)

target_include_directories(MyEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/glad/include
    ${CMAKE_SOURCE_DIR}/src
)

# Lets the game find assets/recipes.json even when run from another folder
target_compile_definitions(MyEngine PRIVATE MYENGINE_ASSETS="${CMAKE_SOURCE_DIR}/assets")

add_subdirectory(external/glfw)
target_link_libraries(MyEngine PRIVATE glfw)

set_target_properties(MyEngine PROPERTIES
    C_STANDARD 17
    CXX_STANDARD 17
)

# Physics check: prints walk/sprint/jump/break numbers next to the wiki values
add_executable(physics_test tests/physics_test.cpp)
target_include_directories(physics_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(physics_test PRIVATE MYENGINE_ASSETS="${CMAKE_SOURCE_DIR}/assets")
set_target_properties(physics_test PROPERTIES CXX_STANDARD 17)
