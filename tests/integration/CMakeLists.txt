add_executable(huynity_scenario_2d_tests scenario_2d_tests.cpp)
target_link_libraries(huynity_scenario_2d_tests PRIVATE
    huynity::core
    huynity::renderer
    huynity::objectlib
    huynity::image
    huynity::render_contracts
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_scenario_2d_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME integration.scenario_2d COMMAND huynity_scenario_2d_tests)

if(TARGET huynity::renderer_actor)
    add_executable(huynity_scenario_3d_tests scenario_3d_tests.cpp)
    target_link_libraries(huynity_scenario_3d_tests PRIVATE
        huynity::core
        huynity::renderer
        huynity::objectlib
        huynity::image
        huynity::render_contracts
        huynity::runtime
        huynity::renderer_actor
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(huynity_scenario_3d_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME integration.scenario_3d COMMAND huynity_scenario_3d_tests)

    add_executable(huynity_benchmark_tests benchmark_tests.cpp)
    target_include_directories(huynity_benchmark_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../engine/renderer/src)
    target_link_libraries(huynity_benchmark_tests PRIVATE
        huynity::core
        huynity::renderer
        huynity::objectlib
        huynity::image
        huynity::render_contracts
        huynity::runtime
        huynity::renderer_actor
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(huynity_benchmark_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME integration.benchmarks COMMAND huynity_benchmark_tests)
endif()

