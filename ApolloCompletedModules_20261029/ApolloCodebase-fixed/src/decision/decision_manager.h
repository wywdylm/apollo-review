// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The Hong Kong University of Science and Technology (Guangzhou), Humanoid Computing & Learning Lab.

#pragma once

#include "src/decision/behavior_tree.h"
#include "src/decision/blackboard.h"
#include "src/decision/high_level_command.h"
#include "src/decision/role_manager.h"
#include "src/decision/role_behaviors.h"
#include "src/decision/penalty_kick_controller.h"
#include "src/decision/match_shot_controller.h"
#include <string>
#include <optional>
#include "src/world/world_snapshot.h"

namespace decision {

/// Owns persistent decision state and evaluates one command per world snapshot.
class DecisionManager {
public:
    explicit DecisionManager(std::string mode = "match");

    HighLevelCommand decide(const world::WorldSnapshot& snapshot);
    const Blackboard& blackboard() const;
    void notify_kick_finished() {
        if (mode_ == "match") match_shot_controller_.complete();
        else penalty_kick_controller_.complete();
    }

private:
    std::string mode_;
    bool match_shooting_enabled_{true};
    MatchShotController match_shot_controller_;
    PenaltyKickController penalty_kick_controller_;
    APState penalty_state_;
    std::optional<double> penalty_ball_seen_time_;
    world::BallState penalty_last_ball_;
    double penalty_goalie_target_y_{0.0};
    std::optional<double> goalie_clear_since_;
    std::optional<double> goalie_clear_until_;

    std::optional<double> penalty_goalie_update_time_;
    std::optional<std::array<double, 2>> penalty_goalie_previous_position_;
    Blackboard blackboard_;
    RoleManager role_manager_;
    BehaviorTree behavior_tree_;
};

}  // namespace decision
