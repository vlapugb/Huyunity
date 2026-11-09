cmake_minimum_required(VERSION 3.20)

project(Huynity LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(HUYNITY_BUILD_EXAMPLES "Build standalone examples" ON)
option(HUYNITY_BUILD_ADAPTERS "Build available adapter components" OFF)

add_subdirectory(engine/math)
add_subdirectory(engine/image)
add_subdirectory(engine/render_contracts)
add_subdirectory(engine/renderer)
add_subdirectory(engine/runtime)
add_subdirectory(engine/objectlib)

if(HUYNITY_BUILD_ADAPTERS)
    foreach(adapter renderer_actor window)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/adapters/${adapter}/CMakeLists.txt)
            add_subdirectory(adapters/${adapter})
        else()
            message(STATUS "Adapter ${adapter} is not implemented; skipping")
        endif()
    endforeach()
endif()

add_subdirectory(engine/core)

if(HUYNITY_BUILD_EXAMPLES)
    add_subdirectory(examples/objectlib)
endif()

if(HUYNITY_BUILD_EXAMPLES AND HUYNITY_BUILD_ADAPTERS)
    add_subdirectory(examples/window_demo)
    add_subdirectory(examples/hello_cube)
    add_subdirectory(examples/renderer_gallery)
    add_subdirectory(examples/game_scene)
    add_subdirectory(examples/first_person_corridor)
    add_subdirectory(examples/third_person_arena)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests/integration)
endif()
