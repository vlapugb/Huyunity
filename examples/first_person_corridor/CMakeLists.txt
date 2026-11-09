add_executable(huynity_first_person_corridor main.cpp)
target_link_libraries(huynity_first_person_corridor PRIVATE
    huynity::engine
    huynity::core
    huynity::renderer
    huynity::runtime
    huynity::image
    huynity::math
    huynity::window
)
set_target_properties(huynity_first_person_corridor PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_first_person_corridor PRIVATE -Wall -Wextra -Wpedantic)
endif()
