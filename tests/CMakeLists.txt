include(Catch)

add_executable(ada_tests
    test_build_info.cpp
    analysis/test_continuity.cpp
    analysis/test_contour.cpp
    analysis/test_differential.cpp
    analysis/test_highlight.cpp
    analysis/test_quality.cpp
    geom/test_bezier_curve.cpp
    geom/test_bezier_patch.cpp
    geom/test_convert.cpp
    geom/test_patch_evaluator.cpp
    mcp/test_server.cpp
    support/panels.cpp
    support/panels.hpp
    support/polynomial.cpp
    support/polynomial.hpp
    # Copied from nurbs2subd together with the code they test (see
    # core/third_party/n2s/SOURCE.md).
    third_party/n2s/test_basis.cpp
    third_party/n2s/test_knot_vector.cpp)

# Lets tests include "support/polynomial.hpp" regardless of their own depth.
target_include_directories(ada_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# The committed example scenes are test input (and test output, for the hidden
# generator case), so the tests need the source tree wherever the build lives.
target_compile_definitions(ada_tests PRIVATE ADA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

target_link_libraries(ada_tests PRIVATE
    ada::core
    ada::mcp_server
    Catch2::Catch2WithMain
    ada::warnings
    ada::sanitizers)

catch_discover_tests(ada_tests)
