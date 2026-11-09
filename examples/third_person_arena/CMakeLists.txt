add_executable(huynity_third_person_arena main.cpp)
target_link_libraries(huynity_third_person_arena PRIVATE
    huynity::engine
    huynity::core
    huynity::renderer
    huynity::runtime
    huynity::image
    huynity::math
    huynity::window
)
set_target_properties(huynity_third_person_arena PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_third_person_arena PRIVATE -Wall -Wextra -Wpedantic)
endif()
