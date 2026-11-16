add_library(huynity_window
    src/headless_window.cpp
    src/window_factory.cpp
)
add_library(huynity::window ALIAS huynity_window)
target_include_directories(huynity_window PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(huynity_window PUBLIC huynity::runtime huynity::image)
target_compile_features(huynity_window PUBLIC cxx_std_23)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_window PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(SDL3 QUIET)
if(SDL3_FOUND AND TARGET SDL3::SDL3)
    set(HUYNITY_WITH_SDL3 ON)
    message(STATUS "Native window backend: SDL3 found")
    target_sources(huynity_window PRIVATE
        src/native_window.cpp
        src/sdl_event_translation.cpp
    )
    find_package(OpenGL REQUIRED)
    target_compile_definitions(huynity_window PUBLIC HUYNITY_HAS_NATIVE_WINDOW SDL_MAIN_HANDLED)
    target_link_libraries(huynity_window PRIVATE SDL3::SDL3 OpenGL::GL)
else()
    message(STATUS "Native window backend not found; defaulting to HeadlessWindow fallback")
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
