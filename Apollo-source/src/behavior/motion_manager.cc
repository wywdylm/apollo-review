// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The Hong Kong University of Science and Technology (Guangzhou), Humanoid Computing & Learning Lab.

#include "src/behavior/motion_manager.h"

#include <variant>
#include <cstdlib>
#include <iostream>

namespace behavior {

MotionManager::MotionManager(const app::RuntimeConfig& config)
    // Keep the tested motion in penalty mode and enable it for gated match shots.
    : experimental_kick_enabled_(
          (config.mode == "penalty-kicker" && !(std::getenv("APOLLO_EXPERIMENTAL_KICK") &&
            std::string(std::getenv("APOLLO_EXPERIMENTAL_KICK")) == "0")) ||
          (config.mode == "match" && !(std::getenv("APOLLO_MATCH_SHOOTING") &&
            std::string(std::getenv("APOLLO_MATCH_SHOOTING")) == "0"))),
      walk_runner_(config.resolve_asset_path("networks/walk/policy.onnx")),
      neutral_runner_(config.resolve_asset_path("keyframes/neutral.yaml")),
      getup_runner_(config.resolve_asset_path("networks/getup/policy.onnx")) {}

MotionStepResult MotionManager::step(
    const world::WorldSnapshot& snapshot,
    const decision::HighLevelCommand& command,
    bool reset) {
    if (std::holds_alternative<decision::BeamCommand>(command)) {
        reset_get_up_state();
        kick_active_ = false; kick_done_ = false; reset_walk_after_kick_ = true;
        walk_ready_ = false; preserve_walk_transition_ = false;
        return {false, "BeamBypass", {}};
    }

    if (!std::holds_alternative<decision::KickCommand>(command)) {
        if (kick_active_) { reset_walk_after_kick_ = true; preserve_walk_transition_ = false; }
        kick_active_ = false;
        kick_done_ = false;
    }
    if (get_up_phase_ != GetUpPhase::Idle) {
        return step_get_up(snapshot, false);
    }

    if (std::holds_alternative<decision::KickCommand>(command)) {
        decision::WalkCommand stop;
        stop.target_absolute = false;
        stop.target_2d_m = {0,0};
        stop.role_id = 0;
        if (snapshot.self.position_m[2] < world::kFallenHeightThresholdM) {
            kick_active_ = false; kick_done_ = true; reset_walk_after_kick_ = true;
            auto result = step_get_up(snapshot, true);
            result.kick_finished = true;
            return result;
        }
        if (!experimental_kick_enabled_ || kick_done_) {
            if (!kick_done_ && !experimental_kick_enabled_)
                std::cerr << "[shoot] candidate kick disabled for this mode or by its runtime switch\n";
            const bool notify = !kick_done_;
            kick_done_ = true;
            auto result = walk_runner_.step(snapshot, stop, reset || reset_walk_after_kick_, 0);
            reset_walk_after_kick_ = false;
            return {true, "KickStopped", result.joint_targets, notify};
        }
        const bool start = !kick_active_;
        if (start) {
            std::cerr << "[shoot] feedback right-foot candidate started; full-client validation pending\n";
            std::cerr << "[shoot] kick_start t=" << snapshot.server_time
                      << " self=" << snapshot.self.position_m[0] << "," << snapshot.self.position_m[1]
                      << " ball_visible=" << snapshot.ball.visible
                      << " ball=" << snapshot.ball.position_m[0] << "," << snapshot.ball.position_m[1]
                      << std::flush << "\n";
        }
        // Preserve policy history across Walk -> Kick -> Walk.
        const auto live = walk_runner_.step(snapshot, stop,
            !walk_ready_ || reset_walk_after_kick_, 0);
        walk_ready_ = true;
        reset_walk_after_kick_ = false;
        auto targets = kick_runner_.step(snapshot, start, live.joint_targets);
        kick_active_ = true;
        const bool finished = kick_runner_.finished(snapshot.server_time);
        if (finished) {
            std::cerr << "[shoot] kick_end t=" << snapshot.server_time
                      << " self=" << snapshot.self.position_m[0] << "," << snapshot.self.position_m[1]
                      << " ball_visible=" << snapshot.ball.visible
                      << " ball=" << snapshot.ball.position_m[0] << "," << snapshot.ball.position_m[1]
                      << std::flush << "\n";
            kick_active_ = false; kick_done_ = true;
            preserve_walk_transition_ = true;
        }
        return {true, finished ? "KickFinished" : "KickExperimental", targets, finished};
    }

    if (const auto* walk = std::get_if<decision::WalkCommand>(&command)) {
        reset_get_up_state();
        const auto result = walk_runner_.step(snapshot, *walk,
            (reset && !preserve_walk_transition_) || reset_walk_after_kick_, walk->role_id);
        walk_ready_ = true;
        preserve_walk_transition_ = false;
        reset_walk_after_kick_ = false;
        return {true, "Walk", result.joint_targets};
    }

    if (std::holds_alternative<decision::NeutralCommand>(command)) {
        reset_get_up_state();
        walk_ready_ = false; preserve_walk_transition_ = false;
        const auto result = neutral_runner_.step(reset, snapshot.server_time);
        return {true, "Neutral", result.joint_targets};
    }

    if (std::holds_alternative<decision::GetUpCommand>(command)) {
        return step_get_up(snapshot, reset);
    }

    return {false, "Idle", {}};
}

MotionStepResult MotionManager::step_get_up(
    const world::WorldSnapshot& snapshot,
    bool reset) {
    walk_ready_ = false; preserve_walk_transition_ = false;
    if (reset || get_up_phase_ == GetUpPhase::Idle) {
        enter_get_up_phase(GetUpPhase::Active, snapshot.server_time);
    }

    auto consume_phase_reset = [&]() {
        const bool phase_reset = get_up_phase_reset_pending_;
        get_up_phase_reset_pending_ = false;
        return phase_reset;
    };

    const auto result = getup_runner_.step(snapshot, consume_phase_reset());
    const bool timed_out =
        snapshot.server_time - get_up_start_time_ >= kGetUpTimeoutS;
    if (result.upright || timed_out) {
        reset_get_up_state();
    }
    return {true, "GetUpRL", result.joint_targets};
}

void MotionManager::enter_get_up_phase(
    GetUpPhase phase,
    double server_time) {
    get_up_phase_ = phase;
    get_up_phase_reset_pending_ = true;
    if (phase == GetUpPhase::Active) {
        get_up_start_time_ = server_time;
    }
}

void MotionManager::reset_get_up_state() {
    get_up_phase_ = GetUpPhase::Idle;
    get_up_phase_reset_pending_ = false;
    get_up_start_time_ = 0.0;
}

}  // namespace behavior
