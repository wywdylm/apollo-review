// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "src/decision/high_level_command.h"
#include "src/robot/joint_targets.h"
#include "src/world/world_snapshot.h"
#include <algorithm>
#include <cmath>
#include <ostream>
#include <string>

namespace app {
// Debug output only; deliberately does not alter decisions or joint commands.
inline void write_motion_diagnostics(std::ostream& out,
    const world::WorldSnapshot& s, const decision::HighLevelCommand& command,
    const std::string& motion, bool reset, const robot::JointTargets& targets) {
    double max_q = 0.0;
    std::size_t bad_targets = 0;
    for (const auto& j : targets) {
        if (!std::isfinite(j.q_deg) || !std::isfinite(j.dq_deg) ||
            !std::isfinite(j.kp) || !std::isfinite(j.kd) || !std::isfinite(j.tau))
            ++bad_targets;
        if (std::isfinite(j.q_deg)) max_q = std::max(max_q, std::abs(j.q_deg));
    }
    out << "[motion] t=" << s.server_time << " player=" << s.player_number
        << " motion=" << motion << " reset=" << reset
        << " xyz=" << s.self.position_m[0] << ',' << s.self.position_m[1] << ',' << s.self.position_m[2]
        << " quat_wxyz=" << s.self.orientation_wxyz[0] << ',' << s.self.orientation_wxyz[1]
        << ',' << s.self.orientation_wxyz[2] << ',' << s.self.orientation_wxyz[3]
        << " gyro_deg_s=" << s.self.gyro_deg_s[0] << ',' << s.self.gyro_deg_s[1] << ',' << s.self.gyro_deg_s[2]
        << " ball_seen=" << s.ball.visible
        << " joint_positions=" << s.self.joint_positions_deg.size()
        << " joint_velocities=" << s.self.joint_velocities_deg_s.size()
        << " targets=" << targets.size() << " nonfinite_targets=" << bad_targets
        << " max_abs_target_deg=" << max_q;
    if (const auto* w = std::get_if<decision::WalkCommand>(&command))
        out << " walk_target=" << w->target_2d_m[0] << ',' << w->target_2d_m[1]
            << " absolute=" << w->target_absolute
            << " orientation_deg=" << w->orientation_deg.value_or(0.0)
            << " has_orientation=" << w->orientation_deg.has_value();

    for (const auto* name : {"Head_yaw", "Head_pitch"}) {
        const auto it = s.self.joint_positions_deg.find(name);
        if (it != s.self.joint_positions_deg.end())
            out << " head_actual_" << name << '=' << it->second;
        for (const auto& joint : targets) {
            if (joint.joint_name == name) {
                out << " head_target_" << name << '=' << joint.q_deg;
                break;
            }
        }
    }
    if (s.ball.visible)
        out << " seen_ball_xyz=" << s.ball.position_m[0]
            << ',' << s.ball.position_m[1]
            << ',' << s.ball.position_m[2];

    out << '\n';
}
} // namespace app
