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

if(TARGET huynity::engine AND TARGET huynity::window AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(engine_lifecycle_stress engine_lifecycle_stress.cpp)
    target_link_libraries(engine_lifecycle_stress PRIVATE huynity::engine)
    add_test(NAME integration.engine_lifecycle_stress COMMAND engine_lifecycle_stress lifecycle 100 30)
    add_test(NAME integration.engine_scene_churn COMMAND engine_lifecycle_stress churn 100 10)
    add_test(NAME integration.engine_scene_churn_gouraud COMMAND engine_lifecycle_stress churn-gouraud 100 10)
    set_tests_properties(integration.engine_lifecycle_stress integration.engine_scene_churn integration.engine_scene_churn_gouraud
        PROPERTIES LABELS stress RUN_SERIAL TRUE TIMEOUT 90)

    find_package(SDL3 QUIET)
    if(TARGET SDL3::SDL3)
        add_executable(engine_resolution_benchmark ../../tools/benchmarks/opengl.cpp)
        target_link_libraries(engine_resolution_benchmark PRIVATE huynity::engine SDL3::SDL3)
        add_test(NAME integration.engine_resolution_smoke COMMAND engine_resolution_benchmark display 3d 1280 720 10)
        set_tests_properties(integration.engine_resolution_smoke
            PROPERTIES LABELS "stress;native" RUN_SERIAL TRUE SKIP_RETURN_CODE 77 TIMEOUT 30)

        add_executable(engine_monitor_test engine_window_stress.cpp)
        target_link_libraries(engine_monitor_test PRIVATE huynity::engine SDL3::SDL3)
        add_test(NAME integration.engine_window_stress COMMAND engine_monitor_test --window-stress)
        set_tests_properties(integration.engine_window_stress
            PROPERTIES LABELS "stress;native" RUN_SERIAL TRUE SKIP_RETURN_CODE 77 TIMEOUT 75)

        # Linker wrapping observes calls from objects linked into the executable.
        if(NOT BUILD_SHARED_LIBS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            add_executable(engine_resource_leak_test engine_gl_resource_stress.cpp)
            target_link_libraries(engine_resource_leak_test PRIVATE huynity::engine SDL3::SDL3)
            target_link_options(engine_resource_leak_test PRIVATE
                -Wl,--wrap=SDL_GL_GetProcAddress,--wrap=SDL_GL_DestroyContext)
            add_test(NAME integration.engine_gl_resource_stress COMMAND engine_resource_leak_test)
            set_tests_properties(integration.engine_gl_resource_stress
                PROPERTIES LABELS "stress;native" RUN_SERIAL TRUE SKIP_RETURN_CODE 77 TIMEOUT 75)
        endif()
    endif()
endif()
