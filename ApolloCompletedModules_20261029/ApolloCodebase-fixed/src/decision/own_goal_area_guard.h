// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "src/decision/high_level_command.h"
#include "src/decision/field_geometry.h"
#include "src/math/math_utils.h"
#include "src/world/world_snapshot.h"
#include <algorithm>
#include <cmath>

namespace decision {
inline HighLevelCommand guard_own_goal_area(
    const world::WorldSnapshot& s, HighLevelCommand command) {
    // Fixed jersey permissions avoid disagreement under local vision.
    if (s.player_number == 1 || s.player_number == 2)
        return command;

    const double boundary =
        -field_geometry::kActualHalfLengthM
        + field_geometry::kGoalieAreaDepthM + 0.6;

    if (auto* beam = std::get_if<BeamCommand>(&command)) {
        if (std::isfinite(beam->x_m))
            beam->x_m = std::max(beam->x_m, boundary);
        return command;
    }
    // Never interrupt recovery from a fall.
    if (std::holds_alternative<GetUpCommand>(command))
        return command;

    const double x = s.self.position_m[0];
    const double y = s.self.position_m[1];
    if (!std::isfinite(x) || !std::isfinite(y))
        return command;

    if (x < boundary) {
        WalkCommand exit;
        exit.target_absolute = true;
        exit.target_2d_m = {boundary + 0.8, y};
        exit.orientation_deg = 0.0;
        exit.orientation_absolute = true;
        if (const auto* walk = std::get_if<WalkCommand>(&command))
            exit.role_id = walk->role_id;
        return exit;
    }

    auto* walk = std::get_if<WalkCommand>(&command);
    if (!walk) return command;
    if (walk->target_absolute) {
        walk->target_2d_m[0] =
            std::max(walk->target_2d_m[0], boundary);
    } else {
        const double yaw =
            world::FrameNormalizer::yaw_deg_from_quaternion_wxyz(
                s.self.orientation_wxyz);
        auto velocity = math::rotate_2d(walk->target_2d_m, yaw);
        // Slow approaching the boundary; forbid inward velocity nearby.
        const double allowed_inward =
            std::max(0.0, x - boundary - 0.6);
        velocity[0] = std::max(velocity[0], -allowed_inward);
        walk->target_2d_m = math::rotate_2d(velocity, -yaw);
    }
    return command;
}
}  // namespace decision
