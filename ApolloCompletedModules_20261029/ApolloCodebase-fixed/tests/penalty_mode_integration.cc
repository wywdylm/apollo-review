#include "src/decision/decision_manager.h"
#include <cassert>
#include <iostream>
int main(){
 world::WorldSnapshot s;s.player_number=1;s.self.position_m={-27,0,.66};
 s.ball.visible=true;s.ball.velocity_valid=true;s.ball.position_m={-26.7,0,.11};
 s.play_mode=world::PlayMode::TheirPenaltyKick;
 decision::DecisionManager formal("penalty-goalie");
 for(int i=0;i<150;++i){s.server_time=i*.02;
  auto w=std::get<decision::WalkCommand>(formal.decide(s));
  assert(w.target_absolute && w.target_2d_m[0]==-27.5);}
 s.play_mode=world::PlayMode::PlayOn;
 decision::DecisionManager local("penalty-goalie");
 bool cleared=false;for(int i=0;i<150;++i){s.server_time=i*.02;
  auto w=std::get<decision::WalkCommand>(local.decide(s));cleared|=w.target_2d_m[0]>-27;}
 assert(cleared); // Existing local-test clearance remains available.
 decision::DecisionManager kicker("penalty-kicker");
 s.self.position_m={10,.1,.66};s.ball.position_m={10.30,0,.11};s.play_mode=world::PlayMode::OurPenaltyKick;
 bool shot=false;for(int i=0;i<40;++i){s.server_time=i*.02;
  shot|=std::holds_alternative<decision::KickCommand>(kicker.decide(s));}
 assert(shot);kicker.notify_kick_finished();
 assert(std::holds_alternative<decision::WalkCommand>(kicker.decide(s)));
 s.play_mode=world::PlayMode::BeforeKickOff;kicker.decide(s);
 s.play_mode=world::PlayMode::OurPenaltyKick;
 shot=false;for(int i=0;i<40;++i){s.server_time=1+i*.02;shot|=std::holds_alternative<decision::KickCommand>(kicker.decide(s));}
 assert(shot);
 std::cout<<"PASS: formal goalie line target/no clearance, preserved PlayOn clearance, kick feedback and referee rearm\n";
}
