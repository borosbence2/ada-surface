include(Catch)

add_executable(ada_tests
    test_build_info.cpp
    geom/test_bezier_curve.cpp
    geom/test_bezier_patch.cpp
    geom/test_convert.cpp
    support/polynomial.cpp
    support/polynomial.hpp
    # Copied from nurbs2subd together with the code they test (see
    # core/third_party/n2s/SOURCE.md).
    third_party/n2s/test_basis.cpp
    third_party/n2s/test_knot_vector.cpp)

# Lets tests include "support/polynomial.hpp" regardless of their own depth.
target_include_directories(ada_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(ada_tests PRIVATE
    ada::core
    Catch2::Catch2WithMain
    ada::warnings
    ada::sanitizers)

catch_discover_tests(ada_tests)
