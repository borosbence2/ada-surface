# core/ is the geometry kernel. Hard rule: it must build and test with no
# graphics library and no network code. Studio and the agent only ever reach
# the kernel through ada-mcp.

add_subdirectory(third_party/n2s)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/build_info.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.cpp"
    @ONLY)

add_library(ada_core STATIC
    include/ada/analysis/budgets.hpp
    include/ada/analysis/continuity.hpp
    include/ada/analysis/contour.hpp
    include/ada/analysis/curvature.hpp
    include/ada/analysis/cv_hull.hpp
    include/ada/analysis/differential.hpp
    include/ada/analysis/highlight.hpp
    include/ada/analysis/quality.hpp
    include/ada/analysis/sampling.hpp
    include/ada/build_info.hpp
    include/ada/doc/scene.hpp
    include/ada/geom/bezier_curve.hpp
    include/ada/geom/bezier_patch.hpp
    include/ada/geom/closest_point.hpp
    include/ada/geom/convert.hpp
    include/ada/geom/patch_evaluator.hpp
    include/ada/geom/vec.hpp
    include/ada/tolerances.hpp
    src/analysis/budgets.cpp
    src/analysis/continuity.cpp
    src/analysis/contour.cpp
    src/analysis/curvature.cpp
    src/analysis/cv_hull.cpp
    src/analysis/differential.cpp
    src/analysis/highlight.cpp
    src/analysis/quality.cpp
    src/analysis/sampling.cpp
    src/doc/scene.cpp
    src/geom/bezier_curve.cpp
    src/geom/bezier_patch.cpp
    src/geom/closest_point.cpp
    src/geom/convert.cpp
    src/geom/patch_evaluator.cpp
    src/geom/polygon_ops.cpp
    src/geom/polygon_ops.hpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.cpp")

add_library(ada::core ALIAS ada_core)

target_include_directories(ada_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        # Implementation-only headers, e.g. "geom/polygon_ops.hpp".
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(ada_core
    PUBLIC
        ada::n2s
        Eigen3::Eigen
        nlohmann_json::nlohmann_json
        fmt::fmt
        ada::sanitizers
    PRIVATE
        ada::warnings)
