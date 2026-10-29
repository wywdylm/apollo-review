// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "src/decision/high_level_command.h"
#include "src/math/math_utils.h"
#include "src/world/world_snapshot.h"
#include <cmath>
#include <algorithm>
#include <optional>
namespace decision {
class PenaltyKickController {
public:
    enum class Phase { Approach, Execute, Done };
    void reset() { *this = PenaltyKickController{}; }
    void complete() { if (phase_ == Phase::Execute) phase_ = Phase::Done; }
    Phase phase() const { return phase_; }
    HighLevelCommand step(const world::WorldSnapshot& s) {
        WalkCommand stop;
        stop.target_absolute = false;
        stop.target_2d_m = {0,0};
        stop.head_search_pitch_deg = 45.0;
        stop.role_id = 0; // Use the goalkeeper head tracker, not the field-player sweep.
        if (s.self.position_m[2] < world::kFallenHeightThresholdM) {
            if (phase_ == Phase::Execute) phase_ = Phase::Done;
            settled_.reset();
            return GetUpCommand{};
        }
        if (phase_ == Phase::Done) return stop;
        if (!std::isfinite(s.self.position_m[0]) || !std::isfinite(s.self.position_m[1]))
            return stop;
        if (phase_ == Phase::Execute) return KickCommand{};
        const std::array<double,2> xy{s.self.position_m[0],s.self.position_m[1]};
        const bool relocated = previous_xy_ &&
            std::hypot(xy[0]-(*previous_xy_)[0],xy[1]-(*previous_xy_)[1])>2.0;
        previous_xy_=xy;
        if (relocated) {
            seen_.reset(); settled_.reset(); last_t_.reset();
            return stop;
        }
        if (last_t_ && s.server_time < *last_t_) {
            seen_.reset(); settled_.reset();
        }
        last_t_ = s.server_time;
        if (s.ball.visible && std::isfinite(s.ball.position_m[0]) &&
            std::isfinite(s.ball.position_m[1]) && std::isfinite(s.ball.position_m[2])) {
            ball_ = s.ball; seen_ = s.server_time;
        }
        if (!seen_ || s.server_time-*seen_ > .25) {
            settled_.reset(); return stop;
        }
        // Wait while a referee placement or another player moves the ball.
        if (ball_.velocity_valid &&
            std::hypot(ball_.velocity_mps[0],ball_.velocity_mps[1]) > .2) {
            settled_.reset(); return stop;
        }
        const double yaw = world::FrameNormalizer::yaw_deg_from_quaternion_wxyz(s.self.orientation_wxyz);
        // Candidate tested with ball 32 cm forward and 8 cm right of body.
        const std::array<double,2> target{ball_.position_m[0]-.32,ball_.position_m[1]+.08};
        WalkCommand approach = stop;
        approach.target_absolute = false;
        approach.orientation_absolute = true;
        approach.orientation_deg = 0.0;
        const double ex=target[0]-s.self.position_m[0], ey=target[1]-s.self.position_m[1];
        // Position gains avoid reusing the match controller's one-to-one small-error command.
        // These are experimental alignment parameters, not a validated foot-contact model.
        // Far from the ball the fine-alignment limits are useless (0.25 m/s means a minute
        // to cover 15 m), so widen them while approaching and tighten only near the spot.
        const double dist_to_ball = std::hypot(ex,ey);
        // The learned gait ignores small velocity requests. Use brief, bounded
        // correction pulses, followed by a quiet interval to measure the settled pose.
        // Do not alter the shared walk policy or its match/goalkeeper gains.
        const double period = dist_to_ball > .35 ? .6 : 1.0;
        const double on_time = dist_to_ball > .35 ? .6 : .12;
        const bool pulse = std::fmod(s.server_time, period) < on_time;
        const auto axis = [pulse](double error, double limit) {
            if (!pulse || std::abs(error)<=.018) return 0.0;
            return std::copysign(limit, error);
        };
        approach.target_2d_m = math::rotate_2d({axis(ex, ex>0 ? .65 : .5), axis(ey,.5)},-yaw);
        const double tilt = std::hypot(s.self.orientation_wxyz[1], s.self.orientation_wxyz[2]);
        const bool still = tilt < .10 && std::hypot(s.self.lin_vel_b[0],s.self.lin_vel_b[1]) < .08
            && std::hypot(s.self.gyro_deg_s[0],s.self.gyro_deg_s[1]) < 15;
        const bool ready = std::abs(ex)<=.02 && std::abs(ey)<=.02
            && std::abs(math::normalize_deg(yaw))<=3 && still
            && ball_.velocity_valid && ball_.position_m[2]<.2
            && std::isfinite(ball_.velocity_mps[0]) && std::isfinite(ball_.velocity_mps[1])
            && std::hypot(ball_.velocity_mps[0],ball_.velocity_mps[1])<.15;
        if (!ready) { settled_.reset(); return approach; }
        if (!settled_) settled_=s.server_time;
        // Stable samples include non-vision frames, whose cached ball is still fresh.
        if (s.server_time-*settled_ >= .5) {
            phase_=Phase::Execute; return KickCommand{};
        }
        // Maintain heading during the pre-kick settling interval.
        stop.orientation_deg = 0.0;
        return stop;
    }
private:
    Phase phase_{Phase::Approach};
    std::optional<double> seen_,settled_,last_t_;
    world::BallState ball_;
    std::optional<std::array<double,2>> previous_xy_;
};
} // namespace decision
