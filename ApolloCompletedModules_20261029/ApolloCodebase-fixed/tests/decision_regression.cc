// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/decision/role_behaviors.cc"
#include "src/decision/behavior_tree.cc"
#include <cassert>
#include <iostream>
using namespace decision;
world::WorldSnapshot sample() {
    world::WorldSnapshot s;
    s.player_number=7; s.self.position_m={-0.424264,-0.424264,0.7};
    s.ball.position_m={0,0,0.1}; s.ball.visible=true;
    s.play_mode=world::PlayMode::OurFreeKick;
    s.play_mode_group=world::PlayModeGroup::OurKick;
    return s;
}
int main() {
    // Release is detected even though the dribble step clears readiness.
    auto s=sample(); Blackboard b; RoleManager rm; APBehavior ap;
    b.set(Blackboard::kKeyCurrentRole,RoleManager::ROLE_AP);
    b.set(Blackboard::kKeyRolePos,std::array<double,2>{-3,2});
    ap.make_command(s,b,rm);
    s.ball.position_m={1,1,0.1}; s.ball.velocity_valid=true;s.ball.velocity_mps={1,1,0};
    ap.make_command(s,b,rm);
    assert(rm.is_self_set_play_pushed(7,s));
    // Fresh ball speed is required: do not infer release from stale estimates.
    RoleManager rm2; APBehavior ap2; auto stale=sample();ap2.make_command(stale,b,rm2);
    stale.ball.position_m={1,1,0.1}; stale.ball.velocity_valid=false;
    ap2.make_command(stale,b,rm2);assert(!rm2.is_self_set_play_pushed(7,stale));
    // The ST's set-play relay must be reachable through the top-level handler.
    auto relay=sample(); relay.player_number=6;
    FormationContext fc;fc.ball_position_m={0,0};fc.play_mode=relay.play_mode;
    auto positions=Formation{}.compute(fc);
    for(int i=0;i<7;++i){world::PlayerObservation p;p.player_number=i+1;p.seen=true;
      p.position_m={positions[i][0],positions[i][1],0.7};relay.teammates.push_back(p);}
    relay.self.position_m=relay.teammates[5].position_m;
    relay.ball.velocity_valid=true;relay.ball.velocity_mps={1,0,0};
    RoleManager rm3;Blackboard bb;BehaviorContext ctx{relay,bb,rm3};
    auto c=make_set_play_command(ctx);
    assert(current_role_from_blackboard(bb)==RoleManager::ROLE_ST);
    assert(std::holds_alternative<WalkCommand>(c));
    assert(!std::get<WalkCommand>(c).target_absolute);
    // A player excluded after a touch must not be elected through comm fallback.
    auto excluded=sample();excluded.self.position_m={0.1,0,0.7};
    for(int i=1;i<=7;++i){world::PlayerObservation p;p.player_number=i;p.seen=true;
      p.position_m={double(i+3),0,0.7};excluded.teammates.push_back(p);}
    excluded.teammates[6].position_m=excluded.self.position_m;
    excluded.teammates[6].comm_role=RoleManager::ROLE_AP;
    excluded.teammates[5].position_m={0.2,0,0.7};
    RoleManager rm4;rm4.mark_self_set_play_pushed(7,excluded);
    for(const auto& a:rm4.assign(excluded))if(a.player_number==7)assert(a.role_id!=RoleManager::ROLE_AP);
    std::cout<<"decision regressions: PASS\n";
}
