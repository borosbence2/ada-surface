#pragma once

// IGES 5.3 export: every patch as one entity 128 (rational B-spline surface;
// here non-rational and single-span, which is exactly a Bézier patch), in
// millimetres. NX, CATIA and Alias all import these as untrimmed sheets, which
// is what the M6 verification needs: our surfaces checked by NX's own
// continuity analysis.

#include "ada/doc/scene.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ada {

struct NamedPatch {
    std::string name;
    BezierPatch patch;
};

/// The complete file text. `timestamp` is IGES's "YYYYMMDD.HHNNSS"; it is a
/// parameter so that the output is reproducible in tests. Throws
/// `std::invalid_argument` for a patch with a degree-0 direction, which a
/// B-spline surface cannot represent.
std::string iges_document(const std::vector<NamedPatch>& patches,
                          const std::string& file_name,
                          const std::string& timestamp);

/// Writes every patch of the scene, stamped with the current time. Throws
/// `std::invalid_argument` if a patch is unsolved (its control points would
/// only be the recipe's starting guess).
void write_iges(const Scene& scene, const std::filesystem::path& path);

} // namespace ada
