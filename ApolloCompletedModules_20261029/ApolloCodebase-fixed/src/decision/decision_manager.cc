// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The Hong Kong University of Science and Technology (Guangzhou), Humanoid Computing & Learning Lab.

#include "src/decision/decision_manager.h"
#include "src/decision/own_goal_area_guard.h"
#include "src/decision/field_geometry.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <utility>

namespace decision {

DecisionManager::DecisionManager(std::string mode) : mode_(std::move(mode)),
    match_shooting_enabled_(!(std::getenv("APOLLO_MATCH_SHOOTING") &&
        std::string(std::getenv("APOLLO_MATCH_SHOOTING")) == "0")) {
    if (mode_ != "match" && mode_ != "penalty-kicker" && mode_ != "penalty-goalie")
        throw std::invalid_argument("Invalid decision mode");
}

HighLevelCommand DecisionManager::decide(const world::WorldSnapshot& snapshot) {
    if (mode_ == "match") {
        auto command = behavior_tree_.evaluate(snapshot, blackboard_, role_manager_);
        if (match_shooting_enabled_) {
            const int role = current_role_from_blackboard(blackboard_);
            if (auto shot = match_shot_controller_.step(snapshot, role, command))
                command = *shot;
        }
        return guard_own_goal_area(snapshot, command);
    }

    // Explicit single-agent roles: never infer a seven-player formation here.
    const bool kicker = mode_ == "penalty-kicker";
    const int role = kicker ? RoleManager::ROLE_AP : RoleManager::ROLE_GK;
    blackboard_.clear();
    blackboard_.set<int>(Blackboard::kKeyCurrentRole, role);
    const std::array<double, 2> hold{
        -field_geometry::kActualHalfLengthM + field_geometry::kGkHoldDepthM, 0.0};
    blackboard_.set<std::array<double, 2>>(Blackboard::kKeyRolePos, hold);
    const bool active = snapshot.play_mode == world::PlayMode::PlayOn ||
        (kicker && (snapshot.play_mode == world::PlayMode::OurPenaltyKick ||
                    snapshot.play_mode == world::PlayMode::OurPenaltyShoot)) ||
        (!kicker && (snapshot.play_mode == world::PlayMode::TheirPenaltyKick ||
                     snapshot.play_mode == world::PlayMode::TheirPenaltyShoot));
    if (!active) {
        penalty_kick_controller_.reset();
        penalty_state_ = {};
        penalty_ball_seen_time_.reset();
        penalty_goalie_update_time_.reset();
        goalie_clear_since_.reset();
        goalie_clear_until_.reset();
        penalty_goalie_previous_position_.reset();
        penalty_goalie_target_y_ = 0.0;
        return NeutralCommand{}; // Referee/server placement; no guessed penalty beam.
    }
    if (kicker) return penalty_kick_controller_.step(snapshot);
    if (snapshot.self.position_m[2] < world::kFallenHeightThresholdM) {
        penalty_state_ = {};
        penalty_ball_seen_time_.reset();
        penalty_goalie_update_time_.reset();
        goalie_clear_since_.reset();
        goalie_clear_until_.reset();
        penalty_goalie_previous_position_.reset();
        penalty_goalie_target_y_ = 0.0;
        return GetUpCommand{};
    }
    // Penalty-only standing interception, in the canonical own-goal-at-minus-x frame.
    // Normal match goalkeeper behavior remains unchanged.
    const bool formal_penalty = snapshot.play_mode == world::PlayMode::TheirPenaltyKick
        || snapshot.play_mode == world::PlayMode::TheirPenaltyShoot;
    // Body-reference target only; actual foot compliance still needs simulator validation.
    const double keeper_x = formal_penalty
        ? -field_geometry::kActualHalfLengthM : hold[0];
    const double limit_y = std::max(0.0, field_geometry::kGoalHalfWidthM - 0.25);
    const std::array<double, 2> self_xy{snapshot.self.position_m[0], snapshot.self.position_m[1]};
    bool relocated = false;
    if (penalty_goalie_previous_position_ &&
        std::hypot(self_xy[0] - (*penalty_goalie_previous_position_)[0],
                   self_xy[1] - (*penalty_goalie_previous_position_)[1]) > 2.0) {
        // A monitor placement/beam must not retain the old smoothed target.
        penalty_ball_seen_time_.reset();
        penalty_goalie_update_time_.reset();
        goalie_clear_since_.reset();
        goalie_clear_until_.reset();
        penalty_goalie_target_y_ = std::clamp(self_xy[1], -limit_y, limit_y);
        relocated = true;
    }
    penalty_goalie_previous_position_ = self_xy;
    double dt = 0.02;
    if (penalty_goalie_update_time_) {
        if (snapshot.server_time < *penalty_goalie_update_time_) {
            penalty_ball_seen_time_.reset();
            penalty_goalie_target_y_ = 0.0;
        } else {
            dt = std::clamp(snapshot.server_time - *penalty_goalie_update_time_, 0.0, 0.1);
        }
    }
    penalty_goalie_update_time_ = snapshot.server_time;
    if (!relocated && snapshot.ball.visible && std::isfinite(snapshot.ball.position_m[0]) &&
        std::isfinite(snapshot.ball.position_m[1])) {
        penalty_last_ball_ = snapshot.ball;
        penalty_ball_seen_time_ = snapshot.server_time;
    }
    double requested_y = penalty_goalie_target_y_;
    if (penalty_ball_seen_time_ &&
        snapshot.server_time - *penalty_ball_seen_time_ <= 0.25 &&
        penalty_last_ball_.position_m[0] > keeper_x) {
        const auto& ball = penalty_last_ball_;
        requested_y = ball.position_m[1];
        // Prefer a short-horizon crossing estimate only for a measured incoming ball.
        if (ball.velocity_valid && std::isfinite(ball.velocity_mps[0]) &&
            std::isfinite(ball.velocity_mps[1]) && ball.velocity_mps[0] < -0.2) {
            const double age = snapshot.server_time - *penalty_ball_seen_time_;
            const double crossing_time = (keeper_x - ball.position_m[0]) / ball.velocity_mps[0];
            if (crossing_time >= age && crossing_time <= 2.0)
                requested_y += crossing_time * ball.velocity_mps[1];
        }
    }
    requested_y = std::clamp(requested_y, -limit_y, limit_y);
    // Limit target changes, rather than snapping between opposite goal posts.
    const double max_step = 0.8 * dt;
    penalty_goalie_target_y_ += std::clamp(
        requested_y - penalty_goalie_target_y_, -max_step, max_step);

    // Local clearance: wait for a slow, nearby ball in front.
    // Keep the same gait and limit both time and departure from goal.
    const double now = snapshot.server_time;
    const double ball_age = penalty_ball_seen_time_
        ? now - *penalty_ball_seen_time_ : 1000.0;
    const auto& clear_ball = penalty_last_ball_;
    const double bx = clear_ball.position_m[0];
    const double by = clear_ball.position_m[1];
    const double dx = bx - self_xy[0];
    const double dy = by - self_xy[1];
    const bool fresh_ball = !relocated && ball_age >= 0.0
        && ball_age <= 0.25
        && std::isfinite(bx) && std::isfinite(by)
        && std::isfinite(clear_ball.position_m[2])
        && clear_ball.position_m[2] < 0.25;
    const bool slow_ball = fresh_ball && clear_ball.velocity_valid
        && std::isfinite(clear_ball.velocity_mps[0])
        && std::isfinite(clear_ball.velocity_mps[1])
        && std::hypot(clear_ball.velocity_mps[0],
                      clear_ball.velocity_mps[1]) < 0.15;
    const bool close_front = !formal_penalty && fresh_ball
        && dx > 0.05 && dx < 0.75 && std::abs(dy) < 0.45
        && self_xy[0] >= keeper_x - 0.2
        && self_xy[0] < keeper_x + 0.8
        && std::abs(self_xy[1]) <= limit_y;

    if (formal_penalty) { goalie_clear_until_.reset(); goalie_clear_since_.reset(); }
    if (goalie_clear_until_ &&
        (now >= *goalie_clear_until_
         || *goalie_clear_until_ - now > 3.1
         || !fresh_ball || dx <= 0.0 || dx > 1.0
         || std::abs(dy) > 0.6
         || self_xy[0] >= keeper_x + 1.0)) {
        goalie_clear_until_.reset();
        goalie_clear_since_.reset();
    }
    if (!goalie_clear_until_) {
        if (slow_ball && close_front) {
            if (!goalie_clear_since_ || now < *goalie_clear_since_)
                goalie_clear_since_ = now;
            if (now - *goalie_clear_since_ >= 0.5) {
                goalie_clear_until_ = now + 3.0;
                goalie_clear_since_.reset();
            }
        } else {
            goalie_clear_since_.reset();
        }
    }
    if (std::getenv("APOLLO_GOALIE_DEBUG") &&
        std::floor(now * 4.0) > std::floor((now - dt) * 4.0)) {
        std::cerr
            << "[goalie_clear] t=" << now
            << " visible=" << snapshot.ball.visible
            << " ball_age=" << ball_age
            << " velocity_valid=" << clear_ball.velocity_valid
            << " speed=" << std::hypot(
                clear_ball.velocity_mps[0],
                clear_ball.velocity_mps[1])
            << " dx=" << dx
            << " dy=" << dy
            << " fresh=" << fresh_ball
            << " slow=" << slow_ball
            << " close=" << close_front
            << " waiting=" << goalie_clear_since_.has_value()
            << " clearing=" << goalie_clear_until_.has_value()
            << std::endl;
    }

    if (goalie_clear_until_) {
        WalkCommand clear;
        clear.target_absolute = true;
        clear.target_2d_m = {
            std::min(self_xy[0] + 0.6, keeper_x + 1.0),
            std::clamp(by, -limit_y, limit_y)};
        clear.orientation_deg = 0.0;
        clear.orientation_absolute = true;
        clear.role_id = RoleManager::ROLE_GK;
        blackboard_.set<std::array<double, 2>>(
            Blackboard::kKeyRolePos, clear.target_2d_m);
        return clear;
    }

    WalkCommand keeper;
    keeper.target_2d_m = {keeper_x, penalty_goalie_target_y_};
    keeper.target_absolute = true;
    keeper.orientation_deg = 0.0; // Face the field, including during lateral movement.
    keeper.orientation_absolute = true;
    keeper.role_id = RoleManager::ROLE_GK;
    blackboard_.set<std::array<double, 2>>(Blackboard::kKeyRolePos, keeper.target_2d_m);
    return keeper;
}

const Blackboard& DecisionManager::blackboard() const {
    return blackboard_;
}

}  // namespace decision
