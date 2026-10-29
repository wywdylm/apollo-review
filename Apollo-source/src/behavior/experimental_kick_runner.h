// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "src/robot/joint_targets.h"
#include "src/world/world_snapshot.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
namespace behavior {
// Residual over LIVE walk policy. Candidate tuned for parallel server one-cycle delay.
// MuJoCo 3.5.0; not validated for sequential/zero-delay operation or official penalties.
class ExperimentalKickRunner {
public:
 static constexpr double kShift=.3232349776031247, kLoad=.3637386257656734;
 static constexpr double kSwing=.10, kRecover=.25;
 static constexpr double kMotionEnd=kShift+kLoad+kSwing+kRecover;
 static constexpr double kFinish=kMotionEnd+1.0;
 static double offset_rad(const std::string& name, double elapsed) {
  if(!std::isfinite(elapsed)||elapsed<0||elapsed>=kMotionEnd)return 0;
  double support=0;
  if(name=="Left_Hip_Roll")support=-.06748338710187295;
  if(name=="Right_Hip_Roll")support=-.06141620921388387;
  if(name=="Left_Ankle_Roll")support=.03372224604991331;
  if(name=="Right_Ankle_Roll")support=-.08943570697677787;
  if(name=="Left_Hip_Pitch")support=-.0028643314471699967;
  if(name=="Left_Knee_Pitch")support=.052906270366163674;
  if(name=="Left_Ankle_Pitch")support=.0028643314471699967-.5*.052906270366163674;
  std::array<double,5> v{0,support,support,support,0};
  if(name=="Right_Hip_Pitch")v={0,0,.3438655097130429,-.7,0};
  if(name=="Right_Knee_Pitch")v={0,0,.4086418184868212,-.1207377699575775,0};
  if(name=="Right_Ankle_Pitch")v={0,0,-.14313362918805278,.036313789765968114,0};
  constexpr std::array<double,5> t{0,kShift,kShift+kLoad,kShift+kLoad+kSwing,kMotionEnd};
  std::size_t i=0;while(i<3&&elapsed>=t[i+1])++i;
  double u=std::clamp((elapsed-t[i])/(t[i+1]-t[i]),0.,1.);
  return v[i]+u*(v[i+1]-v[i]);
 }
 robot::JointTargets step(const world::WorldSnapshot& s,bool start,robot::JointTargets live) {
  if(start){
   start_=s.server_time;initial_xy_={s.self.position_m[0],s.self.position_m[1]};
   for(std::size_t i=0;i<2;++i){
    const char* name=i==0?"Head_yaw":"Head_pitch";
    auto it=s.self.joint_positions_deg.find(name);
    if(it==s.self.joint_positions_deg.end()||!std::isfinite(it->second))
     throw std::runtime_error("Kick requires finite head measurements");
    head_[i]=it->second;
   }
   initialized_=true;aborted_=false;
  }
  if(!initialized_)throw std::runtime_error("Kick runner not initialized");
  double elapsed=s.server_time-start_;
  aborted_=aborted_||!std::isfinite(elapsed)||elapsed<0||
   std::hypot(s.self.position_m[0]-initial_xy_[0],s.self.position_m[1]-initial_xy_[1])>2.;
  if(aborted_)return live;
  for(auto& j:live){
   j.q_deg+=offset_rad(j.joint_name,elapsed)*180./3.14159265358979323846;
   if(j.joint_name=="Head_yaw")j.q_deg=std::clamp(head_[0],-90.,90.);
   if(j.joint_name=="Head_pitch")j.q_deg=std::clamp(head_[1],-20.,70.);
  }
  return live;
 }
 bool finished(double now)const{return aborted_||now-start_>=kFinish;}
private:
 bool initialized_{false},aborted_{false};double start_{0};
 std::array<double,2> initial_xy_{},head_{};
};
}
