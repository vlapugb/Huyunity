find_package(OpenGL REQUIRED)

add_library(huynity_renderer
    src/software_renderer.cpp
    src/gl_functions.cpp
    src/gl_renderer.cpp
)
add_library(huynity::renderer ALIAS huynity_renderer)
target_include_directories(huynity_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(huynity_renderer
    PUBLIC huynity::render_contracts huynity::image
    PRIVATE OpenGL::GL
)
target_compile_features(huynity_renderer PUBLIC cxx_std_23)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
