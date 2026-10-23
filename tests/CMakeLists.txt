include(Catch)

add_executable(ada_tests
    test_build_info.cpp)

target_include_directories(ada_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(ada_tests PRIVATE
    ada::core
    Catch2::Catch2WithMain
    ada::warnings
    ada::sanitizers)

catch_discover_tests(ada_tests)
