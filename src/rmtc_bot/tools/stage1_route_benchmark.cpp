// Development simulator only: fixtures stay here; Planner receives legal own World.
#include "rmtc_bot/core.hpp"
#include <chrono>
#include <iostream>
#include <set>
using namespace rmtc;
int main(int argc,char **argv) {
 bool candidate=argc>1 && (std::string(argv[1])=="--candidate" || std::string(argv[1])=="--joint" || std::string(argv[1])=="--subgoal");
 bool trace=argc>2 && std::string(argv[2])=="--trace";
 Json input;std::cin>>input;Json results=Json::array();
 for(const auto &entry:input) {
  auto config=entry.at("config");int width=config["map"]["width"],height=config["map"]["height"];
  auto position=[](const Json &p){return Pos{p[0].get<int>(),p[1].get<int>()};};
  Pos pos=position(config["robots"][0]["spawn"]);std::set<Pos> walls;
  for(const auto &p:config["map"]["obstacles"])walls.insert(position(p));
  auto resources=config["map"]["resources"];std::set<std::string> collected;
  World world;PlannerConfig cfg;cfg.stage1_resource_route=candidate;cfg.stage1_subgoal_risk=argc>1 && std::string(argv[1])=="--subgoal";cfg.stage1_joint_coverage=argc>1 && (std::string(argv[1])=="--joint" || std::string(argv[1])=="--subgoal");Planner planner(1,100,cfg);
  double value=0,max_ms=0;int invalid=0,high=0,normal=0;Json frames=Json::array();bool moved=false;std::vector<double> decision_times;
  for(int tick=0;tick<100;++tick) {
   Frame f;f.tick=tick;f.width=width;f.height=height;f.radius=config["vision_radius"];
   f.session="local-check";f.team="A";f.pos=pos;f.hp=100;f.alive=true;f.resource_value=value;f.moved=moved;
   auto visible=[&](Pos p){return std::abs(p.x-pos.x)<=f.radius && std::abs(p.y-pos.y)<=f.radius;};
   for(auto p:walls)if(visible(p))f.entities.push_back({"obstacle","","",p,100,0,false,false});
   for(const auto &r:resources)if(!collected.count(r["id"].get<std::string>()) && visible(position(r["position"])))
    f.entities.push_back({r["value"].get<double>()>=20?"resource_high":"resource_normal",r["id"],"",position(r["position"]),100,r["value"],false,false});
   world.update(f);auto begin=std::chrono::steady_clock::now();auto action=planner.decide(world);
   double decision_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
   max_ms=std::max(max_ms,decision_ms);decision_times.push_back(decision_ms);
   if(trace)frames.push_back({{"tick",tick},{"position",{pos.x,pos.y}},{"resource",value},
     {"action",action.json(f)},{"reason",planner.reason},{"target",planner.target()?Json::array({planner.target()->x,planner.target()->y}):Json()},
     {"explored_cells",world.explored.size()},{"known_obstacles",world.obstacles.size()},{"known_resources",world.resources.size()},{"route",planner.stage1_route_evaluation}});
   moved=false;
   if(!legal(action,f,1)){++invalid;continue;}
   if(action.type=="move") {
    Pos next=pos;
    if(action.direction=="up")--next.y;else if(action.direction=="down")++next.y;
    else if(action.direction=="left")--next.x;else if(action.direction=="right")++next.x;
    if(next.x<0||next.x>=width||next.y<0||next.y>=height||walls.count(next)){++invalid;}
    else{pos=next;moved=true;}
   }
   for(const auto &r:resources)if(position(r["position"])==pos && collected.insert(r["id"].get<std::string>()).second) {
    value+=r["value"].get<double>();if(r["value"].get<double>()>=20)++high;else ++normal;
   }
  }
  std::sort(decision_times.begin(),decision_times.end());
  Json result={{"p95_ms",decision_times[94]*.95+decision_times[95]*.05},{"seed",entry.value("seed",0)},{"candidate",candidate},{"resource",value},{"score",value*.2},
   {"high",high},{"normal",normal},{"invalid_actions",invalid},{"max_ms",max_ms}};
  for(const char *key:{"cohort","group"})if(entry.contains(key))result[key]=entry[key];
  if(trace)result["frames"]=frames;
  results.push_back(result);
  if(results.size()%10==0)std::cerr<<"completed "<<results.size()<<"/"<<input.size()<<" development maps\n";
 }
 std::cout<<results.dump(2)<<'\n';
}
