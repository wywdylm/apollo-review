// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The Hong Kong University of Science and Technology (Guangzhou), Humanoid Computing & Learning Lab.

#pragma once

#include <array>
#include <optional>
#include <variant>
#include <vector>

namespace decision {

/// Absolute pose requested from the server beam operation.
struct BeamCommand {
    double x_m{0.0};
    double y_m{0.0};
    double yaw_deg{0.0};
};

/// Planar walking target and optional final orientation.
struct WalkCommand {
    std::array<double, 2> target_2d_m{0.0, 0.0};
    bool target_absolute{true};
    std::optional<double> orientation_deg;
    bool orientation_absolute{true};
    std::optional<int> role_id;
    std::optional<double> head_search_pitch_deg;
};

/// Requests execution of the learned get-up policy.
struct GetUpCommand {};
/// One latched attempt; execution requires an explicitly enabled experimental motion.
struct KickCommand { unsigned long attempt_id{1}; };
/// Requests the neutral standing keyframe.
struct NeutralCommand {};

/// Command variants emitted by the decision layer and consumed by motion control.
using HighLevelCommand = std::variant<BeamCommand, WalkCommand, GetUpCommand, NeutralCommand, KickCommand>;

}  // namespace decision
