#pragma once

#include <Eigen/Core>

namespace ada {

/// A point or vector in model space: millimetres, Z up, Y=0 the vehicle centre
/// plane.
using Vec3 = Eigen::Vector3d;

} // namespace ada
