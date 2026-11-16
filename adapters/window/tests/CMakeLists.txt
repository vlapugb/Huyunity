add_executable(huynity_window_input_tests input_tests.cpp)
target_link_libraries(huynity_window_input_tests PRIVATE huynity::window)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_window_input_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME window.input COMMAND huynity_window_input_tests)

add_executable(huynity_window_adapter_tests window_adapter_tests.cpp)
target_link_libraries(huynity_window_adapter_tests PRIVATE huynity::window)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_window_adapter_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME window.adapter COMMAND huynity_window_adapter_tests)

add_executable(huynity_headless_window_tests headless_window_tests.cpp)
target_link_libraries(huynity_headless_window_tests PRIVATE huynity::window)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_headless_window_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME window.headless COMMAND huynity_headless_window_tests)

add_executable(huynity_window_factory_tests factory_tests.cpp)
target_link_libraries(huynity_window_factory_tests PRIVATE huynity::window)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_window_factory_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME window.factory COMMAND huynity_window_factory_tests)

add_executable(huynity_window_integration_tests window_integration_tests.cpp)
target_link_libraries(huynity_window_integration_tests PRIVATE
    huynity::window
    huynity::runtime
    huynity::image
    huynity::renderer_actor
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(huynity_window_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME window.integration COMMAND huynity_window_integration_tests)


if(HUYNITY_WITH_SDL3)
    add_executable(huynity_sdl_event_translation_tests sdl_event_translation_tests.cpp)
    target_include_directories(huynity_sdl_event_translation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(huynity_sdl_event_translation_tests PRIVATE huynity::window SDL3::SDL3)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(huynity_sdl_event_translation_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME window.sdl_input COMMAND huynity_sdl_event_translation_tests)
endif()

if(HUYNITY_WITH_SDL3)
    add_executable(huynity_sdl_native_window_tests sdl_native_window_tests.cpp)
    target_include_directories(huynity_sdl_native_window_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(huynity_sdl_native_window_tests PRIVATE huynity::window huynity::renderer SDL3::SDL3)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(huynity_sdl_native_window_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME window.sdl_native COMMAND huynity_sdl_native_window_tests)
    set_tests_properties(window.sdl_native PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()
