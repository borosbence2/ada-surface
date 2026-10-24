#pragma once

// The L0-L2 benchmark cases (MILESTONES.md section 2) as authored scenes:
// curves through points and recipes, no control points. Points come from
// analytic shapes and are rounded to 0.1 mm, the way a designer would type
// or digitise them. `ada_tests "[.generate-cases]"` writes them to cases/;
// a normal test fails if the committed files drift from these.

#include "ada/doc/scene.hpp"

#include <string>
#include <vector>

namespace ada::test {

struct AuthoredCase {
    std::string name; ///< File stem under cases/.
    Scene scene;
};

/// L0: a full-width roof centre panel from four boundary curves.
AuthoredCase l0_roof_centre();

/// L0s: the left half of the same roof, mirror-symmetric at Y = 0.
AuthoredCase l0s_roof_centre_sym();

/// L1: two panels at different heights joined by a G2 blend.
AuthoredCase l1_g2_blend();

/// L2: a 2 x 2 hood network, G2 inside, mirror-symmetric at Y = 0.
AuthoredCase l2_hood_2x2_sym();

/// L2: a 3 x 3 door-skin network on a vertical panel, G2 inside.
AuthoredCase l2_panel_3x3();

std::vector<AuthoredCase> all_cases();

} // namespace ada::test
