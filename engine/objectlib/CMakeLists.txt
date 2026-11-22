add_library(huynity_objectlib
    src/geometry.cpp
    src/collections/polygon2.cpp
    src/shapes/shapes.cpp
    src/operations/triangulate.cpp
    src/operations/tessellate.cpp
    src/operations/runtime_operations.cpp
)
add_library(huynity::objectlib ALIAS huynity_objectlib)
target_include_directories(huynity_objectlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(huynity_objectlib PUBLIC huynity::math)
target_compile_features(huynity_objectlib PUBLIC cxx_std_23)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_objectlib PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
