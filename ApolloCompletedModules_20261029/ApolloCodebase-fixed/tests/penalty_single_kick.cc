#include "src/decision/penalty_kick_controller.h"
#include "src/behavior/experimental_kick_runner.h"
#include "src/decision/own_goal_area_guard.h"
#include <cassert>
#include <iostream>
int main(){
 world::WorldSnapshot s; s.self.position_m={10,.1,.66};
 s.ball.visible=true;s.ball.position_m={10.30,0,.11};s.ball.velocity_valid=true;
 decision::PenaltyKickController c;
 // Alternate vision and control frames must not prevent the settle timer completing.
 bool kick=false;
 for(int i=0;i<40;++i){s.server_time=i*.02;s.ball.visible=(i%2==0);auto cmd=c.step(s);
   if(std::holds_alternative<decision::KickCommand>(cmd)){kick=true;break;}}
 assert(kick);
 c.complete();
 for(int i=0;i<100;++i){s.server_time+=.02;assert(!std::holds_alternative<decision::KickCommand>(c.step(s)));}
 c.reset();s.ball.visible=false;s.server_time+=1;
 assert(std::holds_alternative<decision::WalkCommand>(c.step(s)));
 c.reset();s.ball.visible=true;s.ball.velocity_mps={-2,0,0};
 assert(!std::holds_alternative<decision::KickCommand>(c.step(s)));
 s.ball.velocity_mps={0,0,0};
 for(int i=0;i<40;++i){s.server_time+=.02;c.step(s);}
 assert(c.phase()==decision::PenaltyKickController::Phase::Execute);
 s.self.position_m[2]=.1;assert(std::holds_alternative<decision::GetUpCommand>(c.step(s)));
 s.self.position_m[2]=.66;assert(!std::holds_alternative<decision::KickCommand>(c.step(s)));
 robot::T1RobotModel model;
 for(auto& name:model.readable_joint_names())s.self.joint_positions_deg[name]=0;
 behavior::ExperimentalKickRunner runner;s.server_time=10;
 auto first=runner.step(s,true);assert(first.size()==23);
 for(auto& j:first)assert(j.q_deg==0);
 s.server_time=10.75;auto mid=runner.step(s,false);
 bool changed=false;for(auto& j:mid){assert(std::isfinite(j.q_deg));changed|=j.q_deg!=0;}assert(changed);
 s.server_time=11.65;auto end=runner.step(s,false);assert(runner.finished(s.server_time));
 for(auto& j:end)assert(std::abs(j.q_deg)<1e-8);
 s.self.joint_positions_deg.clear();bool threw=false;
 try{runner.step(s,true);}catch(const std::runtime_error&){threw=true;}assert(threw);
 s.player_number=3;s.self.position_m={-24,0,.66};
 auto guarded=decision::guard_own_goal_area(s,decision::WalkCommand{});
 assert(std::get<decision::WalkCommand>(guarded).target_2d_m[0]>-23);
 std::cout<<"PASS: single-shot latch, vision dropout, moving ball wait, fall lockout, pose interpolation, missing joints, area guard\n";
}
