# Files copied from nurbs2subd (see SOURCE.md). Built with the same warning set
# as the rest of the tree; upstream builds clean under it on GCC, Clang and MSVC.

add_library(ada_n2s STATIC
    include/n2s/tolerances.hpp
    include/n2s/nurbs/basis.hpp
    include/n2s/nurbs/detail/binomial.hpp
    include/n2s/nurbs/knot_vector.hpp
    include/n2s/nurbs/surface.hpp
    src/nurbs/basis.cpp
    src/nurbs/knot_vector.cpp
    src/nurbs/surface.cpp)

add_library(ada::n2s ALIAS ada_n2s)

target_include_directories(ada_n2s PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(ada_n2s
    PUBLIC
        Eigen3::Eigen
        fmt::fmt
        ada::sanitizers
    PRIVATE
        ada::warnings)
