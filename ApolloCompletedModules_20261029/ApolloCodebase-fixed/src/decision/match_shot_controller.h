// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "src/decision/penalty_kick_controller.h"
#include "src/decision/role_manager.h"
#include <cmath>
#include <optional>

namespace decision {
// First match integration deliberately keeps the tested straight-ahead shot.
// It does not attempt unvalidated angled shots or moving-ball strikes.
class MatchShotController {
public:
    void complete() {
        if (!engaged_) return;
        controller_.complete();
        hold_until_ = last_time_.value_or(0.0) + .75;
        cooldown_until_ = last_time_.value_or(0.0) + 5.0;
    }
    bool active() const { return engaged_; }
    std::optional<HighLevelCommand> step(const world::WorldSnapshot& s,
                                         int role, const HighLevelCommand& normal) {
        const double now = s.server_time;
        if (!std::isfinite(now)) { reset(); return std::nullopt; }
        const bool relocated=previous_xy_ &&
            std::hypot(s.self.position_m[0]-(*previous_xy_)[0],
                       s.self.position_m[1]-(*previous_xy_)[1])>2.0;
        if ((last_time_ && now < *last_time_) || relocated) reset();
        if (relocated) search_until_=now+2.0;
        last_time_ = now;
        previous_xy_ = std::array<double,2>{s.self.position_m[0],s.self.position_m[1]};
        const bool fallen = s.self.position_m[2] < world::kFallenHeightThresholdM;
        const bool priority = std::holds_alternative<BeamCommand>(normal) ||
                              std::holds_alternative<GetUpCommand>(normal) || fallen;
        // Keep balance through the short goal announcement if a close shot scores
        // before recovery ends, but always yield to a referee beam or fall recovery.
        const bool executing = engaged_ && controller_.phase()==PenaltyKickController::Phase::Execute;
        const bool goal = s.play_mode==world::PlayMode::OurGoal || s.play_mode==world::PlayMode::TheirGoal;
        if (priority || (s.play_mode!=world::PlayMode::PlayOn && !(executing && goal))) {
            cancel(now); seen_.reset(); return std::nullopt;
        }
        if (executing) return controller_.step(s); // latch even if AP changes as ball departs
        if (engaged_ && controller_.phase()==PenaltyKickController::Phase::Done) {
            if (now < hold_until_) return controller_.step(s);
            engaged_=false; controller_.reset();
            return std::nullopt;
        }
        // After referee/monitor placement, do not chase the pre-placement ball
        // estimate out of the shooting lane while the camera still faces level.
        if (now<search_until_ && s.self.position_m[0]>=19.0 && s.self.position_m[0]<=21.0 &&
            role!=RoleManager::ROLE_GK && std::holds_alternative<WalkCommand>(normal) &&
            (relocated || !s.ball.visible)) {
            WalkCommand search; search.target_absolute=false; search.target_2d_m={0,0};
            search.role_id=0; search.head_search_pitch_deg=60.0;
            return search;
        }
        if (s.ball.visible && std::isfinite(s.ball.position_m[0]) &&
            std::isfinite(s.ball.position_m[1]) && std::isfinite(s.ball.position_m[2])) {
            ball_=s.ball; seen_=now;
        }
        // Field-player sweeping points too high to refresh a ball at the feet.
        // Briefly look down using a recent estimate before handing back to the
        // normal dribbler; this also works on natural approaches, not just placement.
        if (!s.ball.visible && seen_ && now-*seen_>.25 && now-*seen_<=1.0 &&
            role==RoleManager::ROLE_AP && s.self.position_m[0]>=19.3 && s.self.position_m[0]<=21.0 &&
            ball_.position_m[0]>=20.0 && ball_.position_m[0]<=21.0 &&
            std::abs(ball_.position_m[1])<=.6 &&
            std::hypot(ball_.position_m[0]-s.self.position_m[0],ball_.position_m[1]-s.self.position_m[1])<.85) {
            WalkCommand search;search.target_absolute=false;search.target_2d_m={0,0};
            search.role_id=0;search.head_search_pitch_deg=60.0;return search;
        }
        const bool fresh=seen_ && now-*seen_<=.25;
        const bool slow=fresh && ball_.velocity_valid &&
            std::isfinite(ball_.velocity_mps[0]) && std::isfinite(ball_.velocity_mps[1]) &&
            std::hypot(ball_.velocity_mps[0],ball_.velocity_mps[1])<.15;
        const bool zone=fresh && ball_.position_m[0]>=20.0 && ball_.position_m[0]<=21.0 &&
            std::abs(ball_.position_m[1])<=.6 && ball_.position_m[2]<.2;
        const bool finite_self=std::isfinite(s.self.position_m[0]) && std::isfinite(s.self.position_m[1]);
        const double dx=ball_.position_m[0]-s.self.position_m[0];
        const double dy=ball_.position_m[1]-s.self.position_m[1];
        bool clear=true;
        const auto check=[&](const world::PlayerObservation& p) {
            const bool recent=p.seen || (p.last_seen_time>=0 && now>=p.last_seen_time && now-p.last_seen_time<=.4);
            if (recent && std::isfinite(p.position_m[0]) && std::isfinite(p.position_m[1]) &&
                std::hypot(p.position_m[0]-ball_.position_m[0],p.position_m[1]-ball_.position_m[1])<.75) clear=false;
        };
        for (const auto& p:s.teammates) if(p.player_number!=s.player_number) check(p);
        for (const auto& p:s.opponents) check(p);
        for (const auto& p:s.shared_opponents) check(p);
        const double yaw=math::normalize_deg(world::FrameNormalizer::yaw_deg_from_quaternion_wxyz(s.self.orientation_wxyz));
        const bool aligned=std::isfinite(yaw) && std::abs(yaw)<=(engaged_ ? 5.0 : 3.0);
        const bool eligible=aligned && role==RoleManager::ROLE_AP && std::holds_alternative<WalkCommand>(normal) &&
            finite_self && slow && zone && dx>.12 && dx<.65 && std::abs(dy)<.4 && clear;
        if (!eligible) { if(engaged_)cancel(now); return std::nullopt; }
        if (!engaged_) {
            if(now<cooldown_until_)return std::nullopt;
            controller_.reset(); engaged_=true; started_at_=now;
        }
        if(now-started_at_>12.0) { cancel(now); return std::nullopt; }
        auto result = controller_.step(s);
        return result;
    }
private:
    void cancel(double now) {
        if(engaged_) cooldown_until_=now+3.0;
        engaged_=false; controller_.reset(); hold_until_=0;
    }
    void reset() {
        controller_.reset(); engaged_=false; started_at_=hold_until_=cooldown_until_=search_until_=0;
        last_time_.reset(); seen_.reset(); previous_xy_.reset();
    }
    PenaltyKickController controller_;
    bool engaged_{false};
    double started_at_{0},hold_until_{0},cooldown_until_{0},search_until_{0};
    std::optional<double> last_time_,seen_;
    std::optional<std::array<double,2>> previous_xy_;
    world::BallState ball_;
};
} // namespace decision
