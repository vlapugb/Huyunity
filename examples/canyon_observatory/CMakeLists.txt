add_executable(huynity_canyon_observatory main.cpp)
target_link_libraries(huynity_canyon_observatory PRIVATE
    huynity::engine
)
set_target_properties(huynity_canyon_observatory PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_canyon_observatory PRIVATE -Wall -Wextra -Wpedantic)
endif()
