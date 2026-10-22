#include "rmtc_bot/core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace rmtc {
namespace {
int integer(const Json &j, const char *name, int lo, int hi) {
  if (!j.is_number_integer()) throw std::invalid_argument(std::string(name)+": integer required");
  auto n = j.get<int64_t>();
  if (n < lo || n > hi) throw std::invalid_argument(std::string(name)+": out of range");
  return static_cast<int>(n);
}
double number(const Json &j, const char *name, double lo=0, double hi=1e12) {
  if (!j.is_number()) throw std::invalid_argument(std::string(name)+": number required");
  double n = j.get<double>();
  if (!std::isfinite(n) || n < lo || n > hi) throw std::invalid_argument(std::string(name)+": out of range");
  return n;
}
std::string string(const Json &j, const char *name, size_t limit=4096) {
  if (!j.is_string() || j.get_ref<const std::string&>().size()>limit)
    throw std::invalid_argument(std::string(name)+": invalid string");
  return j.get<std::string>();
}
bool boolean(const Json &j) {
  if (!j.is_boolean()) throw std::invalid_argument("boolean required");
  return j.get<bool>();
}
Pos position(const Json &j, int width, int height) {
  if (!j.is_array() || j.size()!=2) throw std::invalid_argument("position must be [x,y]");
  return {integer(j[0],"x",0,width-1),integer(j[1],"y",0,height-1)};
}
const std::array<Pos,4> delta{{{0,-1},{0,1},{-1,0},{1,0}}};
const std::array<std::string,4> dirs{{"up","down","left","right"}};
const std::set<std::string> kinds{"explosion","illusion","angel","unrivaled","world_split","pepper","purity","pan"};
Pos add(Pos a, Pos b) { return {a.x+b.x,a.y+b.y}; }
bool aligned(Pos a, Pos b, int radius=2) {
  return (a.x==b.x || a.y==b.y) && distance(a,b)>0 && distance(a,b)<=radius;
}
Json checked_params(const Json &j) {
  if (!j.is_object() || j.size()>24) throw std::invalid_argument("invalid event params");
  for (auto it=j.begin();it!=j.end();++it) number(it.value(),"event parameter",0,100000);
  return j;
}
} // namespace

int distance(Pos a, Pos b) { return std::abs(a.x-b.x)+std::abs(a.y-b.y); }
std::string model_schema_error_code(const std::exception &error) {
  if(auto json_error=dynamic_cast<const Json::exception*>(&error)) {
    if(json_error->id==302) return "json_type";
    if(json_error->id==403) return "json_missing_field";
    return "json_schema_error";
  }
  static const std::map<std::string,std::string> codes{
    {"invalid model schema or session","schema_or_session"},{"model batch too large","batch_limit"},
    {"unknown event kind","event_kind"},{"kind: invalid string","event_kind"},
    {"invalid event rectangle","geometry_shape"},{"reversed event rectangle","geometry_order"},
    {"invalid event points","geometry_shape"},{"position must be [x,y]","position_shape"},
    {"event requires rectangle","missing_rectangle"},{"event requires points","missing_points"},
    {"invalid event params","params_shape"},{"event parameter: number required","parameter_type"},
    {"event parameter: out of range","parameter_bounds"},{"invalid effect identity","effect_identity"},
    {"effect kind: invalid string","effect_identity"},{"effect team: invalid string","effect_identity"},
    {"event history limit","history_limit"},{"x: integer required","coordinate_type"},
    {"y: integer required","coordinate_type"},{"x: out of range","coordinate_bounds"},
    {"y: out of range","coordinate_bounds"},{"start_tick: integer required","tick_type"},
    {"end_tick: integer required","tick_type"},{"start_tick: out of range","tick_bounds"},
    {"end_tick: out of range","tick_bounds"},{"effect start: integer required","tick_type"},
    {"effect end: integer required","tick_type"},{"effect start: out of range","tick_bounds"},
    {"effect end: out of range","tick_bounds"}};
  auto found=codes.find(error.what());return found==codes.end()?"validation_error":found->second;
}
Pos relative_position(const std::string &text) {
  if (text.size()!=89) throw std::invalid_argument("relative grid must contain 89 characters");
  Pos result{}; int count=0;
  for (int y=0;y<9;++y) {
    for (int x=0;x<9;++x) {
      char c=text[y*10+x];
      if (x==4 && y==4) {
        if (c!='1') throw std::invalid_argument("relative grid missing center");
      } else if (c=='2') { result={x-4,y-4}; ++count; }
      else if (c!='0') throw std::invalid_argument("invalid relative grid cell");
    }
    if (y<8 && text[y*10+9]!='\n') throw std::invalid_argument("relative grid newline required");
  }
  if (count>1) throw std::invalid_argument("multiple entities in one relative grid");
  return result;
}

Frame parse_frame(const Json &j) {
  Frame f;
  f.tick=integer(j.at("tick"),"tick",0,1000000);
  f.session=string(j.at("session_id"),"session_id",256);
  if (f.session.empty()) throw std::invalid_argument("empty session_id");
  f.width=integer(j.at("map").at("width"),"width",1,256);
  f.height=integer(j.at("map").at("height"),"height",1,256);
  f.radius=integer(j.at("vision_radius"),"vision_radius",0,4);
  const auto &s=j.at("self");
  f.team=string(s.at("team"),"team",1);
  if (f.team.size()!=1 || std::string("ABCD").find(f.team)==std::string::npos)
    throw std::invalid_argument("invalid team");
  f.pos=position(s.at("position"),f.width,f.height);
  f.hp=number(s.at("hp"),"hp",0,100000);
  f.alive=boolean(s.at("alive")); f.protected_spawn=boolean(s.at("protected"));
  if (s.contains("last_action") && !s["last_action"].is_null()) {
    const auto &a=s.at("last_action");
    f.action_tick=integer(a.at("tick"),"last_action.tick",0,1000000);
    f.moved=a.contains("moved") ? boolean(a.at("moved")):false;
  }
  const auto &entities=j.at("visible_entities");
  if (!entities.is_array() || entities.size()>4096 || (!f.alive && !entities.empty()))
    throw std::invalid_argument("invalid visible_entities");
  for (const auto &e:entities) {
    Entity a;
    a.type=string(e.at("type"),"entity type",64);
    if (a.type!="robot" && a.type!="obstacle" && a.type!="resource_normal" && a.type!="resource_high") continue;
    auto d=relative_position(string(e.at("position"),"relative position",89));
    if (std::abs(d.x)>f.radius || std::abs(d.y)>f.radius) throw std::invalid_argument("entity outside vision");
    a.pos=add(f.pos,d);
    if (a.pos.x<0 || a.pos.x>=f.width || a.pos.y<0 || a.pos.y>=f.height)
      throw std::invalid_argument("entity outside map");
    if (a.type!="obstacle") a.id=string(e.at("id"),"entity id",256);
    if (a.type=="robot") {
      a.team=string(e.at("team"),"entity team",1);
      if (a.team==f.team || a.team.size()!=1 || std::string("ABCD").find(a.team)==std::string::npos)
        throw std::invalid_argument("invalid enemy team");
      a.hp=number(e.at("hp"),"enemy hp",0,100000);
      a.defending=boolean(e.at("defending")); a.protected_spawn=boolean(e.at("protected"));
    } else if (a.type!="obstacle") a.value=number(e.at("value"),"resource value",0,100000);
    f.entities.push_back(a);
  }
  const auto &score=j.at("score");
  f.resource_value=number(score.at("resource_value"),"resource_value");
  f.effective_damage=number(score.at("effective_damage"),"effective_damage");
  if (score.contains("mission_completed_ids")) {
    if (!score["mission_completed_ids"].is_array()) throw std::invalid_argument("invalid completed tasks");
    for (const auto &v:score["mission_completed_ids"]) f.completed.insert(string(v,"mission id",256));
  }
  f.missions=j.at("missions");
  if (!f.missions.is_array() || f.missions.size()>128) throw std::invalid_argument("invalid missions");
  for (const auto &m:f.missions) {
    string(m.at("id"),"mission id",256); auto type=string(m.at("type"),"mission type",64);
    if (type=="reach") position(m.at("position"),f.width,f.height);
    else if (m.contains("target")) number(m["target"],"mission target");
  }
  f.event_text=string(j.at("event_text"),"event_text",131072);
  return f;
}

Json Action::json(const Frame &f) const {
  Json j{{"tick",f.tick},{"session_id",f.session},{"action",type}};
  if (type=="move" || type=="attack") j["direction"]=direction;
  return j;
}
bool legal(const Action &a, const Frame &f, int stage) {
  if (a.type=="wait") return true;
  if (!f.alive || (stage==1 && a.type!="move")) return false;
  if (a.type=="defend") return true;
  if (a.type!="move" && a.type!="attack") return false;
  if (std::find(dirs.begin(),dirs.end(),a.direction)==dirs.end()) return false;
  return a.type!="attack" || !f.protected_spawn;
}
bool Event::contains(Pos p) const {
  if (rect) return p.x>=(*rect)[0].x && p.y>=(*rect)[0].y && p.x<=(*rect)[1].x && p.y<=(*rect)[1].y;
  return std::find(points.begin(),points.end(),p)!=points.end();
}
double Event::number(const std::string &key, double fallback) const { return params.value(key,fallback); }
void Events::clear() { items.clear(); effects.clear(); }
void Events::merge(const Json &j, const Frame &f) {
  if (!j.is_object() || j.at("session_id")!=f.session || !j.at("events").is_array() || !j.at("effects").is_array())
    throw std::invalid_argument("invalid model schema or session");
  if (j["events"].size()>64 || j["effects"].size()>128) throw std::invalid_argument("model batch too large");
  auto new_items=items; auto new_effects=effects;
  for (const auto &v:j["events"]) {
    Event e; e.kind=string(v.at("kind"),"kind",32);
    if (!kinds.count(e.kind)) throw std::invalid_argument("unknown event kind");
    e.start=integer(v.at("start_tick"),"start_tick",0,1000000);
    e.end=integer(v.at("end_tick"),"end_tick",e.start+1,1000001);
    if (v.contains("rect") && !v["rect"].is_null()) {
      const auto &r=v["rect"];
      if (!r.is_array() || r.size()!=2) throw std::invalid_argument("invalid event rectangle");
      auto a=position(r[0],f.width,f.height), b=position(r[1],f.width,f.height);
      if (a.x>b.x || a.y>b.y) throw std::invalid_argument("reversed event rectangle");
      e.rect=std::array<Pos,2>{a,b};
    }
    if (v.contains("points")) {
      if (!v["points"].is_array() || v["points"].size()>64) throw std::invalid_argument("invalid event points");
      for (const auto &p:v["points"]) e.points.push_back(position(p,f.width,f.height));
    }
    if ((e.kind=="explosion" || e.kind=="illusion" || e.kind=="unrivaled") && !e.rect)
      throw std::invalid_argument("event requires rectangle");
    if ((e.kind=="world_split" || e.kind=="angel" || e.kind=="pepper") && e.points.empty())
      throw std::invalid_argument("event requires points");
    e.params=checked_params(v.value("params",Json::object()));
    auto it=std::find_if(new_items.begin(),new_items.end(),[&](const Event &old){
      return old.kind==e.kind && old.start==e.start && old.rect==e.rect && old.points==e.points;
    });
    if (it==new_items.end()) new_items.push_back(e); else *it=e;
  }
  for (const auto &v:j["effects"]) {
    Effect e; e.kind=string(v.at("kind"),"effect kind",32); e.team=string(v.at("team"),"effect team",1);
    if (!std::set<std::string>{"illusion","angel","unrivaled","pan"}.count(e.kind) || e.team.empty() || std::string("ABCD").find(e.team)==std::string::npos)
      throw std::invalid_argument("invalid effect identity");
    e.start=integer(v.at("start_tick"),"effect start",0,1000000);
    e.end=integer(v.at("end_tick"),"effect end",e.start+1,1000001);
    e.params=checked_params(v.value("params",Json::object()));
    auto it=std::find_if(new_effects.begin(),new_effects.end(),[&](const Effect &old){return old.kind==e.kind && old.team==e.team && old.start==e.start;});
    if (it==new_effects.end()) new_effects.push_back(e); else *it=e;
  }
  if (new_items.size()>512 || new_effects.size()>2048) throw std::invalid_argument("event history limit");
  items=std::move(new_items); effects=std::move(new_effects);
}
Json Events::context() const {
  Json out{{"events",Json::array()},{"effects",Json::array()}};
  auto begin=items.size()>32?items.size()-32:0;
  for (size_t i=begin;i<items.size();++i) {
    const auto &e=items[i]; Json r{{"kind",e.kind},{"start_tick",e.start},{"end_tick",e.end},{"params",e.params},{"points",Json::array()}};
    for(auto p:e.points) r["points"].push_back({p.x,p.y});
    if(e.rect) r["rect"]={{(*e.rect)[0].x,(*e.rect)[0].y},{(*e.rect)[1].x,(*e.rect)[1].y}};
    out["events"].push_back(r);
  }
  begin=effects.size()>32?effects.size()-32:0;
  for(size_t i=begin;i<effects.size();++i) {
    const auto &e=effects[i]; out["effects"].push_back({{"kind",e.kind},{"team",e.team},{"start_tick",e.start},{"end_tick",e.end},{"params",e.params}});
  }
  return out;
}

bool World::update(const Frame &f) {
  if (current.session!=f.session) {
    current=Frame{}; history.clear(); explored.clear(); obstacles.clear(); resources.clear(); enemies.clear(); recent_competition.clear(); visits.clear(); events.clear(); spawn.reset();
    normal_collected=high_collected=0;
  } else if (!history.empty() && f.tick<=current.tick) return false;
  if (!history.empty() && (f.width!=current.width || f.height!=current.height || f.team!=current.team))
    throw std::invalid_argument("map/team changed within a session");
  // Do not infer missed collections from total value alone: distinct resource
  // types can have the same sum. Count only previously seen resources at our
  // actual new position, confirmed by a contiguous own score increase.
  if(f.tick==current.tick+1 && f.alive && current.alive && f.resource_value>current.resource_value) {
    int normal=0,high=0;double value=0;
    for(const auto &[id,r]:resources) if(r.pos==f.pos) {
      (void)id;value+=r.value;if(r.value>=20) ++high;else ++normal;
    }
    if(value>0 && value<=f.resource_value-current.resource_value+1e-6) {
      normal_collected+=normal;high_collected+=high;
    }
  }
  current=f; history.push_back(f); if(history.size()>1024) history.pop_front();
  if(f.tick==0) spawn=f.pos;
  if(!f.alive) return true;
  ++visits[f.pos];
  std::set<Pos> now_obstacles; std::set<std::string> now_resources;
  for(const auto &e:f.entities) {
    if(e.type=="obstacle") now_obstacles.insert(e.pos);
    else if(e.type=="robot") enemies[e.team]={e,f.tick};
    else { resources[e.id]={e.pos,e.value,f.tick}; now_resources.insert(e.id); }
  }
  for(int x=std::max(0,f.pos.x-f.radius);x<=std::min(f.width-1,f.pos.x+f.radius);++x)
    for(int y=std::max(0,f.pos.y-f.radius);y<=std::min(f.height-1,f.pos.y+f.radius);++y) {
      Pos p{x,y}; explored.insert(p);
      if(now_obstacles.count(p)) {
        auto it=obstacles.find(p);
        if(it==obstacles.end()) obstacles[p]={f.tick,f.tick}; else it->second.last=f.tick;
      } else obstacles.erase(p);
    }
  for(auto it=resources.begin();it!=resources.end();) {
    if(visible(it->second.pos) && !now_resources.count(it->first)) {
      // A previously observed resource disappeared where a visible opponent
      // now stands. This is a local competition cue, not an opponent score.
      if(it->second.seen>=f.tick-1) for(const auto &e:f.entities)
        if(e.type=="robot" && e.pos==it->second.pos) recent_competition[e.team]=f.tick;
      it=resources.erase(it);
    } else ++it;
  }
  return true;
}
bool World::inside(Pos p) const {return p.x>=0 && p.y>=0 && p.x<current.width && p.y<current.height;}
bool World::visible(Pos p) const {return current.alive && std::abs(p.x-current.pos.x)<=current.radius && std::abs(p.y-current.pos.y)<=current.radius && inside(p);}
bool World::split_line(Pos p,const Event &e) const {
  if(spawn && p==*spawn) return false;
  return !e.points.empty() && (p.x==e.points.front().x || p.y==e.points.front().y);
}
bool World::blocked(Pos p,int tick) const {
  if(!inside(p)) return true;
  auto it=obstacles.find(p); bool observed=it!=obstacles.end();
  for(const auto &e:events.items) if(e.kind=="world_split" && split_line(p,e)) {
    if(e.active(tick)) return true;
    if(observed && it->second.first>=e.start && it->second.last<e.end && tick>=e.end) observed=false;
  }
  return observed;
}
bool World::occupied(Pos p) const {
  for(const auto &e:current.entities) if(e.type=="robot" && e.pos==p) return true;
  return false;
}
const Frame *World::at(int tick) const {
  for(auto it=history.rbegin();it!=history.rend();++it) if(it->tick==tick) return &*it;
  return nullptr;
}
bool World::global(const std::string &kind,int tick) const {
  for(const auto &e:events.items) if(e.kind==kind && e.active(tick)) return true;
  return false;
}
bool World::effect(const std::string &kind,const std::string &team,int tick) const {
  for(const auto &e:events.effects) if(e.kind==kind && e.team==team && e.start<=tick && tick<e.end) return true;
  for(const auto &e:events.items) if(e.kind==kind) {
    if(kind=="illusion") {
      const auto *f=at(e.start); if(!f || tick<e.start || tick>=e.start+e.number("effect_ticks",50)) continue;
      if(team==f->team && f->alive && e.contains(f->pos)) return true;
      for(const auto &v:f->entities) if(v.type=="robot" && v.team==team && e.contains(v.pos)) return true;
    }
    if(team!=current.team) continue;
    if(kind=="angel" || kind=="pan") {
      for(const auto &f:history) {
        int t=f.action_tick;
        if(t<e.start || !e.active(t) || !f.moved || f.tick!=t+1) continue;
        if(kind=="angel" && e.contains(f.pos) && tick>=f.tick && tick<t+e.number("effect_ticks",10)) return true;
        if(kind=="pan" && t<e.start+e.number("movement_window_ticks",20) && tick>=f.tick && tick<e.start+e.number("vulnerability_ticks",e.end-e.start)) return true;
      }
    }
    if(kind=="unrivaled" && tick<e.end) {
      int count=0, previous=-1;
      for(const auto &f:history) if(f.tick>e.start && f.tick<=tick && f.tick<=e.end) {
        count=(f.alive && e.contains(f.pos)) ? (previous==f.tick-1?count+1:1):0;
        previous=f.tick;
        if(count>=e.number("stay_ticks",10)) return true;
      }
    }
  }
  return false;
}
double World::effect_number(const std::string &kind,const std::string &team,int tick,const std::string &key,double fallback) const {
  for(auto it=events.effects.rbegin();it!=events.effects.rend();++it)
    if(it->kind==kind && it->team==team && it->start<=tick && tick<it->end && it->params.contains(key)) return it->params[key].get<double>();
  for(auto it=events.items.rbegin();it!=events.items.rend();++it)
    if(it->kind==kind && it->active(tick) && it->params.contains(key)) return it->params[key].get<double>();
  return fallback;
}
double World::environmental_damage(Pos p,int tick,bool protected_spawn) const {
  double damage=0;
  for(const auto &e:events.items) if(e.start==tick) {
    if(e.kind=="explosion" && e.contains(p) && !protected_spawn) damage+=e.number("damage",80);
    if(e.kind=="world_split" && split_line(p,e)) damage+=100000;
  }
  return damage;
}

Planner::Planner(int stage,int max_ticks,PlannerConfig config):stage_(stage),max_ticks_(max_ticks),config_(config) {
  if(!std::isfinite(config_.early_movement_scale) || config_.early_movement_scale<.1 || config_.early_movement_scale>5
    || !std::isfinite(config_.early_damage_scale) || config_.early_damage_scale<0 || config_.early_damage_scale>1
    || config_.damage_scale_ticks< -1 || config_.damage_scale_ticks>max_ticks_
    || config_.forecast_start_tick< -1 || config_.forecast_start_tick>max_ticks_
    || config_.resource_lookahead<1 || config_.resource_lookahead>3)
    throw std::invalid_argument("invalid planner configuration");
}
Action Planner::decide(const World &w) {
  const auto &f=w.current; reason="wait";
  if(!f.alive || f.tick>=max_ticks_) return {};
  for(const auto &e:w.events.items) if(e.kind=="world_split" && e.start==f.tick && w.split_line(f.pos,e)) {
    reason="forced_respawn"; return {};
  }
  const double hp=f.hp-w.environmental_damage(f.pos,f.tick,f.protected_spawn);
  if(hp<=0) {reason="pre_combat_event_death";return {};}
  auto event_cost=[&](Pos p,int tick,bool protected_spawn) {
    double damage=w.environmental_damage(p,tick,protected_spawn);
    if(damage<=0) return 0.0;
    if(damage>=hp) return 100.0;
    // HP has no direct score value. A survivable event that leaves a healthy
    // reserve should not forfeit an irreplaceable resource or a task. Keep a
    // large penalty for lethal events and dangerously low resulting health.
    return damage*(hp-damage>=50?.008:.15);
  };
  const bool forecast=config_.tactical_forecast && stage_==2
    && f.tick>=(config_.forecast_start_tick<0?max_ticks_/5:config_.forecast_start_tick);
  const int size=f.width*f.height, root=f.pos.y*f.width+f.pos.x;
  std::map<std::string,Pos> velocity;
  if(forecast && w.history.size()>1) {
    const auto &previous=w.history[w.history.size()-2];
    if(previous.tick==f.tick-1) for(const auto &enemy:f.entities) if(enemy.type=="robot")
      for(const auto &before:previous.entities) if(before.type=="robot" && before.team==enemy.team
        && distance(before.pos,enemy.pos)==1)
        velocity[enemy.team]={enemy.pos.x-before.pos.x,enemy.pos.y-before.pos.y};
  }
  auto projected_position=[&](const Entity &enemy,int ticks) {
    Pos pos=enemy.pos;auto motion=velocity.find(enemy.team);
    if(motion==velocity.end()) return pos;
    for(int t=0;t<std::min(ticks,3);++t) {
      Pos next=add(pos,motion->second);
      if(w.blocked(next,f.tick+t) || w.occupied(next)) break;
      pos=next;
    }
    return pos;
  };
  auto normal_ray_clear=[&](Pos attacker,Pos target,const std::string &team,const std::vector<Entity> &robots) {
    if(!aligned(attacker,target)) return false;
    Pos toward{target.x-attacker.x,target.y-attacker.y};
    for(const auto &other:robots) if(other.type=="robot" && other.team!=team
      && aligned(attacker,other.pos) && distance(attacker,other.pos)<distance(attacker,target)) {
      Pos offset{other.pos.x-attacker.x,other.pos.y-attacker.y};
      if(offset.x*toward.x+offset.y*toward.y>0) return false;
    }
    return true;
  };
  const double inf=std::numeric_limits<double>::infinity();
  std::array<double,4> first_information{};
  if(stage_==2 && config_.information_paths) for(int d=0;d<4;++d) {
    Pos next=add(f.pos,delta[d]);int novel=0;
    for(int x=std::max(0,next.x-f.radius);x<=std::min(f.width-1,next.x+f.radius);++x)
      for(int y=std::max(0,next.y-f.radius);y<=std::min(f.height-1,next.y+f.radius);++y)
        if(!w.explored.count({x,y})) ++novel;
    // Reward only the first step's genuinely new strip. Charging every
    // future footprint would repeatedly count the same unobserved cells.
    first_information[d]=.4*novel/(2*f.radius+1);
  }
  std::vector<double> costs(size,inf); std::vector<int> steps(size,0), first(size,-1);
  using Entry=std::pair<double,int>; std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
  costs[root]=0; queue.push({0,root});
  while(!queue.empty()) {
    auto [cost,index]=queue.top();queue.pop(); if(cost>costs[index]+1e-6) continue;
    Pos p{index%f.width,index/f.width};
    for(int d=0;d<4;++d) {
      Pos q=add(p,delta[d]); int arrival=f.tick+steps[index]+1;
      const bool occupied=w.occupied(q);
      // Current occupancy forbids the next action. Beyond that first step,
      // allow a delayed route through an observed robot's cell as a hypothesis;
      // replan from the next perception before actually entering it.
      if(w.blocked(q,arrival-1) || (occupied && (index==root || stage_!=2 || !config_.transient_occupancy))) continue;
      int next=q.y*f.width+q.x;
      double extra=1+(w.explored.count(q)?0:.35)+(occupied?1.0:0.0);
      if(index==root) extra-=first_information[d];
      extra+=std::min(50.0,event_cost(q,arrival,false));
      if(stage_==2) for(const auto &v:f.entities)
        if(v.type=="robot" && !v.protected_spawn && aligned(q,v.pos)) extra+=.6;
      double nc=cost+extra;
      if(nc+1e-6<costs[next]) {costs[next]=nc;steps[next]=steps[index]+1;first[next]=(index==root?d:first[index]);queue.push({nc,next});}
    }
  }
  double best_goal=-1; std::optional<Pos> goal; std::string goal_type="explore";
  auto offer=[&](Pos p,double reward,const std::string &why,double extra_ticks=0) {
    if(!w.inside(p)) return;
    int i=p.y*f.width+p.x;
    if(!std::isfinite(costs[i]) || steps[i]+extra_ticks>max_ticks_-f.tick) return;
    double value=reward/std::pow(costs[i]+extra_ticks+1,.85);
    if(target_ && p==*target_) value*=(stage_==2 && why=="explore" && target_type_=="explore")?1.5:1.12;
    if(value>best_goal) {best_goal=value;goal=p;goal_type=why;}
  };
  struct CollectionMission {std::string type;double remaining;};
  std::vector<CollectionMission> collection_missions;
  if(stage_==2) for(const auto &m:f.missions) if(!f.completed.count(m["id"].get<std::string>())) {
    auto type=m["type"].get<std::string>();
    if(type=="normal_resources" || type=="high_resources" || type=="resource_value") {
      double progress=type=="normal_resources"?w.normal_collected:(type=="high_resources"?w.high_collected:f.resource_value);
      collection_missions.push_back({type,std::max(1.0,m.value("target",1.0)-progress)});
    }
    if(type=="reach") {
      Pos p{m["position"][0],m["position"][1]};
      // Resources disappear permanently; a coordinate task can be done later.
      // Restore its urgency early enough to travel there before the deadline.
      int remaining=max_ticks_-f.tick;
      double reward=(f.tick>max_ticks_/3 || remaining<distance(f.pos,p)+30)?6:1;
      offer(p,reward,"mission");
    }
  }
  auto task_reward=[&](double value,int normal,int high,double collected_value) {
    double result=0;
    for(const auto &mission:collection_missions) {
      double before=mission.type=="normal_resources"?normal:(mission.type=="high_resources"?high:collected_value);
      double increment=mission.type=="normal_resources"?(value<20?1:0):(mission.type=="high_resources"?(value>=20?1:0):value);
      result+=5*(std::min(mission.remaining,before+increment)-std::min(mission.remaining,before))/mission.remaining;
    }
    return result;
  };
  struct RivalRoute {Pos pos,motion;int age;bool moving;std::vector<int> steps;};
  std::vector<RivalRoute> rivals;
  if(stage_==2 && config_.race_aware_resources) for(const auto &[team,record]:w.enemies) {
    int age=f.tick-record.second;if(age>4) continue;
    RivalRoute rival{record.first.pos,{},age,false,std::vector<int>(size,-1)};
    if(age==0 && w.history.size()>1) {
      const auto &previous=w.history[w.history.size()-2];
      if(previous.tick==f.tick-1) for(const auto &before:previous.entities)
        if(before.type=="robot" && before.team==team && distance(before.pos,rival.pos)==1) {
          rival.motion={rival.pos.x-before.pos.x,rival.pos.y-before.pos.y};rival.moving=true;
        }
    }
    // Hypothetical enemy routes use only our known walls. Unknown cells are
    // assumed passable for an optimistic arrival estimate, not added as facts.
    std::vector<int> frontier(size);int read=0,write=0;
    int origin=rival.pos.y*f.width+rival.pos.x;frontier[write++]=origin;rival.steps[origin]=0;
    while(read<write) {
      int index=frontier[read++];Pos p{index%f.width,index/f.width};
      for(auto step:delta) {
        Pos q=add(p,step);if(w.blocked(q,f.tick)) continue;
        int next=q.y*f.width+q.x;if(rival.steps[next]>=0) continue;
        rival.steps[next]=rival.steps[index]+1;frontier[write++]=next;
      }
    }
    rivals.push_back(std::move(rival));
  }
  auto capture_probability=[&](Pos resource,double own_arrival) {
    double probability=1;
    for(const auto &rival:rivals) {
      int length=rival.steps[resource.y*f.width+resource.x];if(length<0) continue;
      double intent=.55;
      if(rival.moving) {
        int toward=(resource.x-rival.pos.x)*rival.motion.x+(resource.y-rival.pos.y)*rival.motion.y;
        intent=toward>0?.85:(toward<0?.35:.55);
      }
      intent*=std::exp(-rival.age/3.0);
      double arrival=std::max(0,length-rival.age);
      double beats_us=1/(1+std::exp(std::clamp((arrival-own_arrival)/1.5,-30.0,30.0)));
      probability=std::min(probability,1-intent*beats_us);
    }
    return probability;
  };
  struct ResourceOption {Pos pos;double reward,cost,value,task_bonus,probability,competition;int steps;};
  std::vector<ResourceOption> resource_options;
  for(const auto &[id,r]:w.resources) {
    (void)id;
    double reward=stage_==1?r.value*.2:std::min(r.value,std::max(0.0,120-f.resource_value))*.25;
    double task_bonus=stage_==2?task_reward(r.value,0,0,0):0;
    reward+=task_bonus;
    if(stage_==2) {
      // Break otherwise equal resource routes by the information acquired on
      // arrival, using only our explored cells. IDs and spawn labels carry no
      // tactical meaning and must not determine a geometric tie.
      int gain=0;
      for(int x=std::max(0,r.pos.x-f.radius);x<=std::min(f.width-1,r.pos.x+f.radius);++x)
        for(int y=std::max(0,r.pos.y-f.radius);y<=std::min(f.height-1,r.pos.y+f.radius);++y)
          if(!w.explored.count({x,y})) ++gain;
      reward+=.02*gain;
    }
    double confidence=std::exp(-std::max(0,f.tick-r.seen-8)/60.0);
    // During the race for finite resources, a safe resource left behind near
    // our own start can be revisited. Do not let it force a long return trip
    // while unexplored territory ahead may already be contested. This is a
    // soft geometric prior, never a claim about unseen opponents or resources.
    double return_priority=1;
    if(stage_==2 && w.spawn && f.tick<max_ticks_/5 && w.explored.size()<static_cast<size_t>(size*.75)
      && distance(*w.spawn,r.pos)+3<distance(*w.spawn,f.pos)) return_priority=.6;
    // Estimate competition only from legitimately observed enemy positions.
    double competition=1;
    for(const auto &[team,record]:w.enemies) {
      (void)team;
      if(f.tick-record.second>4) continue;
      int enemy_distance=distance(record.first.pos,r.pos);
      int own_distance=steps[r.pos.y*f.width+r.pos.x];
      if(enemy_distance+2<own_distance) competition=std::min(competition,.4);
    }
    if(stage_==2 && config_.race_aware_resources)
      competition=capture_probability(r.pos,steps[r.pos.y*f.width+r.pos.x]);
    double probability=confidence*competition*return_priority;
    double expected=std::max(.1,reward)*probability;
    offer(r.pos,expected,"resource");
    double cost=costs[r.pos.y*f.width+r.pos.x];
    if(stage_==2 && std::isfinite(cost) && steps[r.pos.y*f.width+r.pos.x]<=max_ticks_-f.tick)
      resource_options.push_back({r.pos,expected,cost,r.value,task_bonus,probability,competition,steps[r.pos.y*f.width+r.pos.x]});
  }
  if(stage_==2 && config_.resource_lookahead>1) {
    // A bounded three-stop rollout values resources collected along a route.
    // The first leg uses the actual local path cost. Later legs can use a BFS
    // snapshot of remembered walls, or Manhattan estimates for comparison.
    // No unseen resources or opponent states are introduced by this search.
    std::stable_sort(resource_options.begin(),resource_options.end(),[](const auto &a,const auto &b){
      return a.reward/std::pow(a.cost+1,.85)>b.reward/std::pow(b.cost+1,.85);
    });
    if(resource_options.size()>16) resource_options.resize(16);
    std::vector<std::vector<int>> pair_steps(resource_options.size(),std::vector<int>(resource_options.size(),-1));
    if(config_.resource_paths_exact) {
      // Only our remembered obstacles and model-validated current walls enter
      // this snapshot. Future enemy positions are not treated as fixed walls.
      std::vector<bool> walkable(size);
      for(int index=0;index<size;++index) walkable[index]=!w.blocked({index%f.width,index/f.width},f.tick);
      std::vector<int> frontier(size),length(size);
      for(size_t i=0;i<resource_options.size();++i) {
        std::fill(length.begin(),length.end(),-1);
        Pos start=resource_options[i].pos;int origin=start.y*f.width+start.x;
        int read=0,write=0;frontier[write++]=origin;length[origin]=0;
        while(read<write) {
          int index=frontier[read++];Pos p{index%f.width,index/f.width};
          for(auto step:delta) {
            Pos q=add(p,step);if(!w.inside(q)) continue;int next=q.y*f.width+q.x;
            if(!walkable[next] || length[next]>=0) continue;
            length[next]=length[index]+1;frontier[write++]=next;
          }
        }
        for(size_t j=0;j<resource_options.size();++j) {
          Pos p=resource_options[j].pos;pair_steps[i][j]=length[p.y*f.width+p.x];
        }
      }
    } else for(size_t i=0;i<resource_options.size();++i) for(size_t j=0;j<resource_options.size();++j)
      pair_steps[i][j]=distance(resource_options[i].pos,resource_options[j].pos);
    auto follow_reward=[&](const ResourceOption &r,int normal,int high,double collected_value,int arrival) {
      double first_points=std::min(r.value,std::max(0.0,120-f.resource_value))*.25;
      double future_points=std::min(r.value,std::max(0.0,120-f.resource_value-collected_value))*.25;
      double reward=std::max(0.0,r.reward+(task_reward(r.value,normal,high,collected_value)-r.task_bonus
        +future_points-first_points)*r.probability);
      if(config_.race_aware_resources) reward*=capture_probability(r.pos,arrival)/r.competition;
      return reward;
    };
    for(size_t i=0;i<resource_options.size();++i) {
      const auto &a=resource_options[i];
      double first_value=a.reward/std::pow(a.cost+1,.85), value=first_value;
      for(size_t j=0;j<resource_options.size();++j) if(j!=i) {
        if(pair_steps[i][j]<0) continue;
        const auto &b=resource_options[j];double elapsed=a.cost+pair_steps[i][j];
        if(elapsed>max_ticks_-f.tick) continue;
        int normal=a.value<20?1:0,high=a.value>=20?1:0;
        int arrival=a.steps+pair_steps[i][j];
        double second=first_value+.65*follow_reward(b,normal,high,a.value,arrival)/std::pow(elapsed+1,.85);
        value=std::max(value,second);
        if(config_.resource_lookahead<3) continue;
        for(size_t k=0;k<resource_options.size();++k) if(k!=i && k!=j) {
          if(pair_steps[j][k]<0) continue;
          const auto &c=resource_options[k];double end=elapsed+pair_steps[j][k];
          if(end<=max_ticks_-f.tick) value=std::max(value,second+.4*follow_reward(c,
            normal+(b.value<20?1:0),high+(b.value>=20?1:0),a.value+b.value,arrival+pair_steps[j][k])/std::pow(end+1,.85));
        }
      }
      offer(a.pos,value*std::pow(a.cost+1,.85),"resource");
    }
  }
  if(stage_==2) for(const auto &e:w.events.items) if(e.end>f.tick) {
    if(e.kind=="angel" && f.hp<80) for(auto p:e.points) {
      int i=p.y*f.width+p.x;
      if(!config_.event_timing) {
        if(f.tick+steps[i]>=e.start && f.tick+steps[i]<e.end) offer(p,(100-f.hp)*.045,"heal");
      } else if(!w.effect("angel",f.team,f.tick) && std::isfinite(costs[i])) {
        // N moves beginning in Tick t enter on t+N-1, not t+N.
        // A point appearing under a stationary robot does not grant healing.
        int entry=f.tick+steps[i]-1;
        double reward=std::min(100-f.hp,e.number("heal_per_tick",10)*e.number("effect_ticks",10))*.045;
        if(steps[i]>0 && entry>=e.start && entry<e.end && entry+1<max_ticks_)
          offer(p,reward,"heal");
        else if(entry<e.start || p==f.pos) for(auto d:delta) {
          Pos adjacent=add(p,d);if(!w.inside(adjacent)) continue;
          int j=adjacent.y*f.width+adjacent.x;
          if(!std::isfinite(costs[j])) continue;
          // Finish staging at t+N-1; the earliest entry is the next action t+N.
          int enter=std::max(e.start,f.tick+steps[j]);
          if(enter>=e.end || enter+1>=max_ticks_) continue;
          int wait=enter-(f.tick+steps[j]);
          offer(adjacent,reward,"heal_stage",wait+1);
        }
      }
    }
    if(e.kind=="unrivaled" && e.rect && !w.effect("unrivaled",f.team,f.tick))
      for(int x=(*e.rect)[0].x;x<=(*e.rect)[1].x;++x) for(int y=(*e.rect)[0].y;y<=(*e.rect)[1].y;++y) {
        Pos p{x,y};int i=y*f.width+x;
        if(!config_.event_timing) {
          if(f.tick+steps[i]+e.number("stay_ticks",10)<e.end) offer(p,5,"fortress");
        } else if(std::isfinite(costs[i])) {
          int arrival=f.tick+std::max(0,steps[i]-1);
          int begin=std::max(arrival,e.start),residence=0;
          if(p==f.pos && e.active(f.tick)) {
            int previous=f.tick+1;
            for(auto it=w.history.rbegin();it!=w.history.rend() && it->tick>e.start;++it) {
              if(previous-it->tick!=1 || !it->alive || !e.contains(it->pos)) break;
              ++residence;previous=it->tick;
            }
          }
          int needed=std::max(1,static_cast<int>(e.number("stay_ticks",10))-residence);
          int acquired=begin+needed;
          int usable=std::min(e.end,max_ticks_)-acquired;
          if(usable>0) {
            double reward=5.0*usable/(usable+needed);
            offer(p,reward,"fortress",begin-arrival+needed-1);
          }
        }
      }
  }
  struct Frontier {Pos pos;double reward,cost;};
  std::vector<Frontier> frontiers;
  const bool explore_route=config_.frontier_lookahead && stage_==2 && w.resources.empty()
    && f.tick<max_ticks_/5 && w.explored.size()<static_cast<size_t>(size*.75);
  for(int i=0;i<size;++i) {
    if(i==root || !std::isfinite(costs[i])) continue;
    Pos p{i%f.width,i/f.width}; int gain=0;
    for(int x=std::max(0,p.x-f.radius);x<=std::min(f.width-1,p.x+f.radius);++x)
      for(int y=std::max(0,p.y-f.radius);y<=std::min(f.height-1,p.y+f.radius);++y) if(!w.explored.count({x,y})) ++gain;
    if(gain) {
      // With no known resources, value the full new footprint rather than
      // treating a thin strip along an empty edge like a large unseen region.
      double information=(stage_==2 && w.resources.empty())?gain/20.0:std::min(1.0,gain/20.0);
      offer(p,(stage_==1?2.3:1.3)*information,"explore");
      if(explore_route) frontiers.push_back({p,1.3*information,costs[i]});
    }
  }
  if(explore_route) {
    // Compare short exploration tours rather than only their final footprint.
    // All rewards are unobserved cells, never invented resource locations.
    // Separate nearby candidate centers so the bounded search is not filled
    // by sixteen nearly identical, overlapping footprints.
    std::stable_sort(frontiers.begin(),frontiers.end(),[](const auto &a,const auto &b) {
      return a.reward/std::pow(a.cost+1,.85)>b.reward/std::pow(b.cost+1,.85);
    });
    std::vector<Frontier> candidates;
    for(const auto &frontier:frontiers) {
      bool separated=std::all_of(candidates.begin(),candidates.end(),[&](const auto &other) {
        return std::max(std::abs(frontier.pos.x-other.pos.x),std::abs(frontier.pos.y-other.pos.y))>f.radius;
      });
      if(separated) candidates.push_back(frontier);
      if(candidates.size()==16) break;
    }
    for(const auto &a:candidates) {
      double value=a.reward/std::pow(a.cost+1,.85);
      for(const auto &b:candidates) if(b.pos!=a.pos) {
        double elapsed=a.cost+distance(a.pos,b.pos);
        if(elapsed>max_ticks_-f.tick) continue;
        int novel=0;
        for(int x=std::max(0,b.pos.x-f.radius);x<=std::min(f.width-1,b.pos.x+f.radius);++x)
          for(int y=std::max(0,b.pos.y-f.radius);y<=std::min(f.height-1,b.pos.y+f.radius);++y)
            if(!w.explored.count({x,y}) && (std::abs(x-a.pos.x)>f.radius || std::abs(y-a.pos.y)>f.radius)) ++novel;
        value=std::max(value,a.reward/std::pow(a.cost+1,.85)+.55*1.3*novel/20.0/std::pow(elapsed+1,.85));
      }
      offer(a.pos,value*std::pow(a.cost+1,.85),"explore");
    }
  }
  // Pursue reachable firing positions, not the enemy's occupied cell. Continue
  // earning legitimate damage after the point cap: it breaks otherwise tied
  // scores. Finite resources and unfinished missions retain their own value.
  if(stage_==2) {
    bool hunt=f.tick>max_ticks_/5 || w.explored.size()>static_cast<size_t>(size*.75);
    for(const auto &[team,record]:w.enemies) {
      auto competition=w.recent_competition.find(team);
      bool contest=config_.early_interception && competition!=w.recent_competition.end() && f.tick-competition->second<=5;
      if(!hunt && !contest) continue;
      if(f.tick-record.second>6 || record.first.protected_spawn) continue;
      if(forecast) {
        // Arrive at a firing cell when the forecast enemy could still be hit.
        // Do not compare a two-step own route to a one-step enemy prediction.
        if(record.second==f.tick && velocity.count(team) && !aligned(f.pos,record.first.pos)
          && aligned(f.pos,projected_position(record.first,1))) offer(f.pos,1.8,"ambush");
        for(int horizon=0;horizon<=2;++horizon) {
          Pos enemy=projected_position(record.first,horizon);
          for(int d=0;d<4;++d) for(int range=1;range<=2;++range) {
            Pos p{enemy.x+delta[d].x*range,enemy.y+delta[d].y*range};
            if(!w.inside(p)) continue;
            int index=p.y*f.width+p.x;
            if(!std::isfinite(costs[index]) || (!hunt && steps[index]>2)) continue;
            bool immediate=p==f.pos && aligned(p,record.first.pos);
            bool timed=aligned(p,projected_position(record.first,steps[index]));
            if(!hunt && !immediate && !timed) continue;
            double confidence=(immediate || timed)?1:.35;
            offer(p,1.8*confidence/(1+f.tick-record.second),"engage");
          }
        }
        continue;
      }
      Pos predicted=record.first.pos;
      if(record.second==f.tick && w.history.size()>1) {
        const auto &previous=w.history[w.history.size()-2];
        if(previous.tick==f.tick-1) for(const auto &before:previous.entities)
          if(before.type=="robot" && before.team==team && distance(before.pos,record.first.pos)==1) {
            Pos next{2*record.first.pos.x-before.pos.x,2*record.first.pos.y-before.pos.y};
            if(w.inside(next) && !w.blocked(next,f.tick) && !w.occupied(next)) predicted=next;
          }
      }
      for(int projected=0;projected<2;++projected) {
        Pos enemy=projected?predicted:record.first.pos;
        if(projected && enemy==record.first.pos) continue;
        for(int d=0;d<4;++d) for(int range=1;range<=2;++range) {
          Pos p{enemy.x+delta[d].x*range,enemy.y+delta[d].y*range};
          if(hunt || (w.inside(p) && costs[p.y*f.width+p.x]<=2))
            offer(p,(projected?2.0:1.8)/(1+f.tick-record.second),"engage");
        }
      }
    }
  }
  if(!goal && stage_==2) {
    for(const auto &[team,record]:w.enemies) { (void)team;if(f.tick-record.second<20) offer(record.first.pos,2,"pursue"); }
    if(!goal) for(int i=0;i<size;++i) {
      Pos p{i%f.width,i/f.width};if(i==root) continue;
      auto visit=w.visits.find(p);offer(p,1.0/(1+(visit==w.visits.end()?0:visit->second)),"patrol");
    }
  }
  std::optional<int> searched_first;
  if(config_.resource_beam && stage_==2 && f.tick<max_ticks_/5 && size<=4096
    && (goal_type=="resource" || goal_type=="explore" || goal_type=="engage" || goal_type=="ambush")
    && !(goal && *goal==f.pos && goal_type=="resource")) {
    auto candidates=resource_options;
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto &a,const auto &b) {
      return a.reward/std::pow(a.cost+1,.85)>b.reward/std::pow(b.cost+1,.85);
    });
    if(candidates.size()>16) candidates.resize(16);
    constexpr int horizon=8,beam_width=32;
    bool nearby=candidates.empty() || std::any_of(candidates.begin(),candidates.end(),[](const auto &r){return r.steps<=horizon;});
    if(nearby) {
      struct RouteState {
        Pos pos;std::array<Pos,horizon+1> trail{};std::vector<uint64_t> seen;
        uint32_t collected{};int first{-1},normal{},high{};double value{},resource_value{},hp{},priority{};
        std::optional<Pos> first_resource;
      };
      RouteState initial;initial.pos=f.pos;initial.trail[0]=f.pos;initial.hp=hp;initial.seen.resize((size+63)/64);
      for(auto p:w.explored) {int index=p.y*f.width+p.x;initial.seen[index/64]|=uint64_t{1}<<(index%64);}
      std::vector<RouteState> beam{std::move(initial)};
      double best_rate=0;std::optional<RouteState> selected;
      for(int depth=1;depth<=horizon && f.tick+depth<=max_ticks_ && !beam.empty();++depth) {
        std::vector<RouteState> expanded;
        std::map<std::tuple<int,int,uint32_t>,size_t> retained;
        double discount=std::pow(.95,depth);
        for(const auto &parent:beam) for(int direction=0;direction<4;++direction) {
          Pos next=add(parent.pos,delta[direction]);
          if(w.blocked(next,f.tick+depth-1) || (depth==1 && w.occupied(next))) continue;
          bool repeated=false;for(int t=0;t<depth;++t) if(parent.trail[t]==next) repeated=true;
          if(repeated) continue;
          double damage=w.environmental_damage(next,f.tick+depth,false);
          if(damage>=parent.hp) continue;
          RouteState state=parent;state.pos=next;state.trail[depth]=next;state.hp-=damage;
          if(depth==1) state.first=direction;
          int novel=0;
          for(int x=std::max(0,next.x-f.radius);x<=std::min(f.width-1,next.x+f.radius);++x)
            for(int y=std::max(0,next.y-f.radius);y<=std::min(f.height-1,next.y+f.radius);++y) {
              int index=y*f.width+x;uint64_t bit=uint64_t{1}<<(index%64);
              if(!(state.seen[index/64]&bit)) {state.seen[index/64]|=bit;++novel;}
            }
          double reward=.035*novel-.025-damage*(state.hp>=50?.008:.15);
          for(size_t i=0;i<candidates.size();++i) if(!(state.collected&(uint32_t{1}<<i)) && candidates[i].pos==next) {
            const auto &r=candidates[i];double probability=r.probability;
            if(config_.race_aware_resources) probability*=capture_probability(next,depth)/r.competition;
            reward+=(std::min(r.value,std::max(0.0,120-f.resource_value-state.resource_value))*.25
              +task_reward(r.value,state.normal,state.high,state.resource_value))*probability;
            state.collected|=uint32_t{1}<<i;state.resource_value+=r.value;
            if(r.value>=20) ++state.high;else ++state.normal;
            if(!state.first_resource) state.first_resource=next;
          }
          state.value+=discount*reward;
          // The potential only retains promising partial paths in the beam;
          // final selection credits actual hypothetical pickups and unique
          // new cells, never a resource invented outside our own memory.
          double potential=0;
          for(size_t i=0;i<candidates.size();++i) if(!(state.collected&(uint32_t{1}<<i)))
            potential=std::max(potential,candidates[i].reward/(distance(next,candidates[i].pos)+1));
          state.priority=state.value+.6*potential;
          double rate=state.value/std::pow(depth+1,.85);
          if(rate>best_rate) {best_rate=rate;selected=state;}
          auto key=std::make_tuple(state.first,next.y*f.width+next.x,state.collected);
          auto found=retained.find(key);
          if(found==retained.end()) {retained[key]=expanded.size();expanded.push_back(std::move(state));}
          else if(state.priority>expanded[found->second].priority) expanded[found->second]=std::move(state);
        }
        std::stable_sort(expanded.begin(),expanded.end(),[](const auto &a,const auto &b){return a.priority>b.priority;});
        if(expanded.size()>beam_width) expanded.resize(beam_width);
        beam=std::move(expanded);
      }
      if(selected) {
        searched_first=selected->first;goal=selected->first_resource?selected->first_resource:std::optional<Pos>(selected->pos);
        goal_type=selected->first_resource?"resource":"explore";best_goal=best_rate;
      }
    }
  }
  target_=goal;target_type_=goal_type;int preferred=-1;
  if(goal) preferred=first[goal->y*f.width+goal->x];
  if(searched_first) preferred=*searched_first;
  const bool reverse=w.effect("illusion",f.team,f.tick), self_unrivaled=w.effect("unrivaled",f.team,f.tick);
  const bool purity=w.global("purity",f.tick);
  double reflection=20;
  for(const auto &e:w.events.items) if(e.kind=="purity" && e.active(f.tick)) reflection=e.number("reflection_damage",20);
  double incoming=0;
  if(stage_==2 && !f.protected_spawn) for(const auto &v:f.entities) if(v.type=="robot" && !v.protected_spawn) {
    if(forecast && w.environmental_damage(v.pos,f.tick,v.protected_spawn)>=v.hp) continue;
    bool un=w.effect("unrivaled",v.team,f.tick);
    int radius=static_cast<int>(w.effect_number("unrivaled",v.team,f.tick,"attack_radius",2));
    bool ray=forecast?normal_ray_clear(v.pos,f.pos,v.team,f.entities):aligned(v.pos,f.pos);
    if((un && std::max(std::abs(v.pos.x-f.pos.x),std::abs(v.pos.y-f.pos.y))<=radius) || (!un && ray))
      incoming+=un?w.effect_number("unrivaled",v.team,f.tick,"attack_damage",50):20;
  }
  const double vulnerable=w.effect("pan",f.team,f.tick)?w.effect_number("pan",f.team,f.tick,"damage_multiplier",2):1;
  bool available_finisher=false;
  if(config_.productive_escape && !f.protected_spawn) for(const auto &enemy:f.entities)
    if(enemy.type=="robot" && !enemy.protected_spawn) {
      int radius=static_cast<int>(w.effect_number("unrivaled",f.team,f.tick,"attack_radius",2));
      bool reachable=self_unrivaled?std::max(std::abs(enemy.pos.x-f.pos.x),std::abs(enemy.pos.y-f.pos.y))<=radius
        :normal_ray_clear(f.pos,enemy.pos,f.team,f.entities);
      double damage=self_unrivaled?w.effect_number("unrivaled",f.team,f.tick,"attack_damage",50):20;
      if(w.effect("pan",enemy.team,f.tick))damage*=w.effect_number("pan",enemy.team,f.tick,"damage_multiplier",2);
      if(reachable && damage>=enemy.hp)available_finisher=true;
    }
  std::vector<Action> actions{{"wait",""}};
  if(stage_==2) {actions.push_back({"defend",""});if(!f.protected_spawn) for(const auto &d:dirs) actions.push_back({"attack",d});}
  for(const auto &d:dirs) actions.push_back({"move",d});
  Action best; double best_score=-inf;
  for(const auto &a:actions) {
    double score=a.type=="wait"?-.3:-.15, received=incoming*vulnerable;
    std::map<std::string,double> dealt;
    Pos end=f.pos; int actual=-1;
    if(a.type=="move") {
      int command=std::find(dirs.begin(),dirs.end(),a.direction)-dirs.begin();actual=reverse?(command^1):command;
      end=add(f.pos,delta[actual]);if(w.blocked(end,f.tick) || w.occupied(end)) continue;
      // Keep action selection connected to the value of the chosen plan.
      double plan_bonus=(goal_type=="engage" || goal_type=="ambush")?.6:std::clamp(best_goal,.7,2.5);
      if(stage_==2 && (goal_type=="resource" || goal_type=="explore")
        && f.tick<max_ticks_/5 && w.explored.size()<static_cast<size_t>(size*.75))
        plan_bonus*=config_.early_movement_scale;
      score=.1+(actual==preferred?plan_bonus:0);
      if(config_.productive_escape && stage_==2 && f.tick<max_ticks_/5
        && (goal_type=="resource" || goal_type=="explore") && actual==preferred
        && !f.protected_spawn && !available_finisher && incoming*vulnerable>0 && incoming*vulnerable<hp) {
        // Moving does not evade this Tick's combat. Credit only a productive
        // next position with less exposure to currently observed firing cells.
        // Enemy movement remains uncertain; this is not a predicted fact.
        double current_fire=0,next_fire=0;
        for(const auto &enemy:f.entities) if(enemy.type=="robot" && !enemy.protected_spawn) {
          bool current_un=w.effect("unrivaled",enemy.team,f.tick);
          int current_radius=static_cast<int>(w.effect_number("unrivaled",enemy.team,f.tick,"attack_radius",2));
          if((current_un && std::max(std::abs(enemy.pos.x-f.pos.x),std::abs(enemy.pos.y-f.pos.y))<=current_radius)
            || (!current_un && normal_ray_clear(enemy.pos,f.pos,enemy.team,f.entities)))
            current_fire+=current_un?w.effect_number("unrivaled",enemy.team,f.tick,"attack_damage",50):20;
          bool un=w.effect("unrivaled",enemy.team,f.tick+1);
          int radius=static_cast<int>(w.effect_number("unrivaled",enemy.team,f.tick+1,"attack_radius",2));
          if((un && std::max(std::abs(enemy.pos.x-end.x),std::abs(enemy.pos.y-end.y))<=radius)
            || (!un && normal_ray_clear(enemy.pos,end,enemy.team,f.entities)))
            next_fire+=un?w.effect_number("unrivaled",enemy.team,f.tick+1,"attack_damage",50):20;
        }
        score+=std::min(1.2,std::max(0.0,current_fire-next_fire)*vulnerable*.045);
      }
      if(goal) score+=.12*(distance(f.pos,*goal)-distance(end,*goal));
      auto visit=w.visits.find(end);if(visit!=w.visits.end()) score-=std::min(.5,visit->second*.025);
      int recent=0;
      for(auto it=w.history.rbegin();it!=w.history.rend() && f.tick-it->tick<=6;++it)
        if(it->alive && it->pos==end) ++recent;
      if(stage_==2) score-=std::min(1.2,recent*.25);
      if(forecast) for(const auto &enemy:f.entities) if(enemy.type=="robot"
        && velocity.count(enemy.team) && projected_position(enemy,1)==end) score-=.6;
      // The next activation occurs before the next move: leave danger one Tick early.
      score-=event_cost(end,f.tick+1,false);
      if(stage_==2) for(const auto &e:w.events.items) if(e.active(f.tick)) {
        if(e.kind=="angel" && e.contains(end) && (!config_.event_timing || !w.effect("angel",f.team,f.tick)))
          score+=std::min(100-f.hp,e.number("heal_per_tick",10)*e.number("effect_ticks",10))*.025;
        if(e.kind=="pepper") for(auto point:e.points) if(point!=end && (point.x==end.x || point.y==end.y))
          for(const auto &v:f.entities) if(v.type=="robot" && distance(v.pos,point)==1) score-=e.number("damage",50)*.08;
      }
    } else if(a.type=="defend") {
      received=0;
      if(!purity && !f.protected_spawn) for(const auto &v:f.entities) if(v.type=="robot" && !v.protected_spawn) {
        if(forecast && w.environmental_damage(v.pos,f.tick,v.protected_spawn)>=v.hp) continue;
        bool un=w.effect("unrivaled",v.team,f.tick);
        int radius=static_cast<int>(w.effect_number("unrivaled",v.team,f.tick,"attack_radius",2));
        bool ray=forecast?normal_ray_clear(v.pos,f.pos,v.team,f.entities):aligned(v.pos,f.pos);
        if((un && std::max(std::abs(v.pos.x-f.pos.x),std::abs(v.pos.y-f.pos.y))<=radius) || (!un && ray))
          received+=(un?w.effect_number("unrivaled",v.team,f.tick,"defended_damage",10):10)*vulnerable;
      }
    } else if(a.type=="attack") {
      std::vector<const Entity*> targets; int dir=std::find(dirs.begin(),dirs.end(),a.direction)-dirs.begin();
      for(const auto &v:f.entities) if(v.type=="robot") {
        if(forecast && w.environmental_damage(v.pos,f.tick,v.protected_spawn)>=v.hp) continue;
        auto d=Pos{v.pos.x-f.pos.x,v.pos.y-f.pos.y};
        int radius=static_cast<int>(w.effect_number("unrivaled",f.team,f.tick,"attack_radius",2));
        if(self_unrivaled) {if(std::max(std::abs(d.x),std::abs(d.y))<=radius) targets.push_back(&v);}
        else if(aligned(v.pos,f.pos) && d.x*delta[dir].x+d.y*delta[dir].y>0) targets.push_back(&v);
      }
      if(!self_unrivaled && targets.size()>1) {
        auto it=std::min_element(targets.begin(),targets.end(),[&](auto a1,auto b1){return distance(a1->pos,f.pos)<distance(b1->pos,f.pos);});
        auto nearest=*it;targets={nearest};
      }
      score=-.25;
      for(auto v:targets) if(!v->protected_spawn) {
        double defend_probability=v->defending?.65:.15;
        double base=self_unrivaled?w.effect_number("unrivaled",f.team,f.tick,"attack_damage",50):20;
        double defended=self_unrivaled?w.effect_number("unrivaled",f.team,f.tick,"defended_damage",10):10;
        double mult=w.effect("pan",v->team,f.tick)?w.effect_number("pan",v->team,f.tick,"damage_multiplier",2):1;
        double damage=((1-defend_probability)*base+(purity?0:defend_probability*defended))*mult;
        double available_hp=forecast?std::max(0.0,v->hp-w.environmental_damage(v->pos,f.tick,v->protected_spawn)):v->hp;
        dealt[v->team]=std::min(available_hp,damage);
        int discount_until=config_.damage_scale_ticks<0?max_ticks_/5:config_.damage_scale_ticks;
        // Finite resources can be lost permanently; ordinary damage can
        // usually be earned later. Preserve the existing score for a possible
        // immediate finisher, even when a defended shot may not finish it.
        double scale=(f.tick<discount_until && base*mult<available_hp)?config_.early_damage_scale:1;
        score+=std::min(available_hp,damage)*.09*scale;
        if(damage>=available_hp) score+=.9;
        if(purity) received+=defend_probability*reflection;
      }
    }
    // Being in the fortress after combat counts this Tick; acquisition helps next Tick.
    if(stage_==2 && !self_unrivaled) for(const auto &e:w.events.items)
      if(e.kind=="unrivaled" && e.active(f.tick) && e.contains(end)) {
        int residence=0,previous=f.tick+1;
        for(auto it=w.history.rbegin();it!=w.history.rend() && it->tick>e.start;++it) {
          if(previous-it->tick!=1 || !it->alive || !e.contains(it->pos)) break;
          ++residence;previous=it->tick;
        }
        // Attack and defend also count as post-combat residence. Preserve
        // confirmed progress rather than restarting a full stay-time estimate.
        if(e.end-f.tick>=std::max(1.0,e.number("stay_ticks",10)-residence)) score+=.9;
      }
    score-=received*.045;
    if(received>=hp) score-=3;
    if(forecast && stage_==2 && received<hp) {
      std::vector<Entity> future;
      for(auto enemy:f.entities) if(enemy.type=="robot") {
        enemy.hp-=w.environmental_damage(enemy.pos,f.tick,enemy.protected_spawn)+dealt[enemy.team];
        enemy.pos=projected_position(enemy,1);
        enemy.hp-=w.environmental_damage(enemy.pos,f.tick+1,enemy.protected_spawn);
        if(enemy.hp>0) future.push_back(enemy);
      }
      double exposure=0,best_damage=0;
      bool next_protected=f.protected_spawn && a.type!="move";
      bool next_purity=w.global("purity",f.tick+1);
      double next_multiplier=w.effect("pan",f.team,f.tick+1)?w.effect_number("pan",f.team,f.tick+1,"damage_multiplier",2):1;
      if(a.type=="move") for(const auto &event:w.events.items) if(event.kind=="pan" && event.active(f.tick)
        && f.tick<event.start+event.number("movement_window_ticks",20)) next_multiplier=event.number("damage_multiplier",2);
      for(const auto &enemy:future) if(!next_protected && !enemy.protected_spawn) {
        bool un=w.effect("unrivaled",enemy.team,f.tick+1);
        int radius=static_cast<int>(w.effect_number("unrivaled",enemy.team,f.tick+1,"attack_radius",2));
        if((un && std::max(std::abs(enemy.pos.x-end.x),std::abs(enemy.pos.y-end.y))<=radius)
          || (!un && normal_ray_clear(enemy.pos,end,enemy.team,future)))
          exposure+=(un?w.effect_number("unrivaled",enemy.team,f.tick+1,"attack_damage",50):20)*next_multiplier;
      }
      bool next_unrivaled=w.effect("unrivaled",f.team,f.tick+1);
      int radius=static_cast<int>(w.effect_number("unrivaled",f.team,f.tick+1,"attack_radius",2));
      if(!next_protected) for(int d=0;d<4;++d) {
        std::vector<const Entity*> targets;
        for(const auto &enemy:future) {
          Pos offset{enemy.pos.x-end.x,enemy.pos.y-end.y};
          if(next_unrivaled) {if(std::max(std::abs(offset.x),std::abs(offset.y))<=radius) targets.push_back(&enemy);}
          else if(aligned(end,enemy.pos) && offset.x*delta[d].x+offset.y*delta[d].y>0) targets.push_back(&enemy);
        }
        if(!next_unrivaled && targets.size()>1) {
          auto nearest=*std::min_element(targets.begin(),targets.end(),[&](auto x,auto y){return distance(end,x->pos)<distance(end,y->pos);});
          targets={nearest};
        }
        double damage=0;
        for(auto enemy:targets) if(!enemy->protected_spawn) {
          double defend=enemy->defending?.65:.15;
          double base=next_unrivaled?w.effect_number("unrivaled",f.team,f.tick+1,"attack_damage",50):20;
          double guarded=next_unrivaled?w.effect_number("unrivaled",f.team,f.tick+1,"defended_damage",10):10;
          double multiplier=w.effect("pan",enemy->team,f.tick+1)?w.effect_number("pan",enemy->team,f.tick+1,"damage_multiplier",2):1;
          damage+=std::min(enemy->hp,((1-defend)*base+(next_purity?0:defend*guarded))*multiplier);
          if(next_purity) {
            double next_reflection=20;
            for(const auto &event:w.events.items) if(event.kind=="purity" && event.active(f.tick+1))
              next_reflection=event.number("reflection_damage",20);
            damage-=defend*next_reflection;
          }
        }
        best_damage=std::max(best_damage,damage);
      }
      score+=best_damage*.035-exposure*.02;
      double future_hp=hp-received-w.environmental_damage(end,f.tick+1,next_protected);
      if(future_hp>0 && exposure>=future_hp) score-=1;
      if(goal_type=="ambush" && goal && *goal==f.pos && a.type!="move") score+=.35;
    }
    if(a.type!="move") score-=event_cost(end,f.tick+1,f.protected_spawn);
    // Collection and coordinate tasks are checked after movement. When the
    // current cell is the goal, staying alive here completes it; moving away
    // would forfeit the objective even though the route has already arrived.
    if(stage_==2 && goal && *goal==f.pos && (goal_type=="resource" || goal_type=="mission") && a.type!="move")
      score+=std::min(4.0,best_goal);
    if(stage_==2 && config_.event_timing && goal && *goal==f.pos && goal_type=="heal_stage" && a.type!="move")
      score+=std::min(4.0,best_goal);
    if(stage_==1 && goal && *goal==f.pos && a.type=="wait") score=2;
    if(score>best_score) {best_score=score;best=a;}
  }
  reason=goal_type;
  if(best.type=="attack" || best.type=="defend") reason="combat";
  if(!legal(best,f,stage_)) return {};
  return best;
}
} // namespace rmtc
