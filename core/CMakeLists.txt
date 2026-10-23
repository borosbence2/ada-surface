# core/ is the geometry kernel. Hard rule: it must build and test with no
# graphics library and no network code. Studio and the agent only ever reach
# the kernel through ada-mcp.

add_subdirectory(third_party/n2s)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/build_info.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.cpp"
    @ONLY)

add_library(ada_core STATIC
    include/ada/build_info.hpp
    include/ada/geom/bezier_curve.hpp
    include/ada/geom/bezier_patch.hpp
    include/ada/geom/convert.hpp
    include/ada/geom/vec.hpp
    src/geom/bezier_curve.cpp
    src/geom/bezier_patch.cpp
    src/geom/convert.cpp
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
