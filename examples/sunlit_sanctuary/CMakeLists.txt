add_executable(huynity_sunlit_sanctuary main.cpp)
target_link_libraries(huynity_sunlit_sanctuary PRIVATE
    huynity::engine
)
set_target_properties(huynity_sunlit_sanctuary PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_sunlit_sanctuary PRIVATE -Wall -Wextra -Wpedantic)
endif()
