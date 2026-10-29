// Runner doubles isolate MotionManager's lifecycle from inference and physics.
#include "src/behavior/motion_manager.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
namespace behavior {
OnnxSession::OnnxSession(const std::filesystem::path&,const OnnxModelContract&){}
WalkRunner::WalkRunner(const std::filesystem::path& p):session_(p,{}){}
WalkStepResult WalkRunner::step(const world::WorldSnapshot&,const decision::WalkCommand&,bool,std::optional<int>){return {};}
GetupRunner::GetupRunner(const std::filesystem::path& p):session_(p,{}){}
GetupStepResult GetupRunner::step(const world::WorldSnapshot&,bool){return {{},true};}
KeyframeRunner::KeyframeRunner(const std::filesystem::path&){}
KeyframeStepResult KeyframeRunner::step(bool,double){return {{},true};}
}
int main(){
 app::RuntimeConfig cfg;cfg.mode="penalty-kicker";
 world::WorldSnapshot s;s.self.position_m[2]=.66;
 robot::T1RobotModel model;for(auto& n:model.readable_joint_names())s.self.joint_positions_deg[n]=0;
 unsetenv("APOLLO_EXPERIMENTAL_KICK");behavior::MotionManager disabled(cfg);
 auto off=disabled.step(s,decision::KickCommand{},true);
 assert(off.active_motion=="KickStopped" && off.kick_finished);
 assert(!disabled.step(s,decision::KickCommand{},false).kick_finished);
 setenv("APOLLO_EXPERIMENTAL_KICK","1",1);behavior::MotionManager motion(cfg);
 auto start=motion.step(s,decision::KickCommand{},true);assert(!start.kick_finished);
 s.server_time=.8;assert(!motion.step(s,decision::KickCommand{},true).kick_finished);
 s.server_time=1.7;assert(motion.step(s,decision::KickCommand{},false).kick_finished);
 s.server_time=3;auto repeated=motion.step(s,decision::KickCommand{},true);
 assert(repeated.active_motion=="KickStopped" && !repeated.kick_finished);
 motion.step(s,decision::NeutralCommand{},true);
 assert(motion.step(s,decision::KickCommand{},true).active_motion=="KickExperimental");
 s.self.position_m[2]=.1;auto fallen=motion.step(s,decision::KickCommand{},false);
 assert(fallen.active_motion=="GetUpRL" && fallen.kick_finished);
 cfg.mode="match";s.self.position_m[2]=.66;behavior::MotionManager match(cfg);
 assert(match.step(s,decision::KickCommand{},true).active_motion=="KickStopped");
 std::cout<<"PASS: completion event, no repeated replay/reset, disabled default, fall abort, match isolation (runner doubles)\n";
}
