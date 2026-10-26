// Development-only replay of legal OWN observations from real stage-one games.
// No map fixture, oracle, referee state or hidden entity is accepted here.
#include "rmtc_bot/core.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace rmtc;
namespace {
Json without_timing(Json value) {
  if(value.is_object())value.erase("elapsed_ms");
  return value;
}
}
int main() {
  Json files;std::cin>>files;Json results=Json::array();int total=0,errors=0;
  for(const auto &path:files){std::ifstream stream(path.get<std::string>());
    if(!stream)throw std::runtime_error("Cannot open own observation log");
    PlannerConfig config;config.stage1_resource_route=config.stage1_joint_coverage=config.stage1_subgoal_risk=true;
    Planner planner(1,100,config);World world;int count=0;Json mismatches=Json::array();std::string line;
    while(std::getline(stream,line)){auto record=Json::parse(line);if(record.value("kind","")!="decision")continue;
      Frame f;f.tick=record.at("tick");f.session=record.at("session_id");f.team="A";
      f.width=record.at("map_size")[0];f.height=record.at("map_size")[1];f.radius=record.at("vision_radius");
      f.pos={record.at("position")[0],record.at("position")[1]};f.hp=record.at("hp");
      f.alive=record.at("alive");f.protected_spawn=record.at("protected");
      f.resource_value=record.at("resource_value");f.effective_damage=record.at("effective_damage");
      f.missions=record.at("public_missions");f.completed=record.at("completed").get<std::set<std::string>>();
      const auto &receipt=record.at("last_action");f.moved=receipt.is_object() && receipt.value("moved",false);
      for(const auto &point:record.at("known_obstacles")){Pos p{point[0],point[1]};
        if(std::abs(p.x-f.pos.x)<=f.radius && std::abs(p.y-f.pos.y)<=f.radius)
          f.entities.push_back({"obstacle","","",p,100,0,false,false});}
      for(const auto &r:record.at("observed_resources")){double value=r.at("value");
        f.entities.push_back({value==20?"resource_high":"resource_normal",r.at("id"),"",
          {r.at("position")[0],r.at("position")[1]},100,value,false,false});}
      if(!record.at("observed_enemies").empty() || !f.missions.empty() || f.tick!=count)
        throw std::runtime_error("Only complete single-car resource-stage own logs are supported");
      world.update(f);
      if(world.resources.size()!=record.at("known_resources") || world.explored.size()!=record.at("explored_cells")
          || world.obstacles.size()!=record.at("known_obstacles").size())
        throw std::runtime_error("Own memory reconstruction differs");
      auto action=planner.decide(world);
      Json target=planner.target()?Json::array({planner.target()->x,planner.target()->y}):Json();
      bool action_same=action.json(f)==record.at("action"),target_same=target==record.at("target");
      bool values_same=without_timing(planner.stage1_route_evaluation)==without_timing(record.at("stage1_route_evaluation"));
      if(!action_same || !target_same || !values_same){++errors;
        mismatches.push_back({{"tick",f.tick},{"action_same",action_same},{"target_same",target_same},{"values_same",values_same}});}
      ++count;++total;
    }
    if(count!=100)throw std::runtime_error("Incomplete own log");
    results.push_back({{"path",path},{"frames",count},{"mismatches",mismatches}});
  }
  std::cout<<Json({{"frames",total},{"mismatches",errors},{"results",results}}).dump(2)<<'\n';
  return errors?1:0;
}
