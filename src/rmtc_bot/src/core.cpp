#include "rmtc_bot/core.hpp"
#include <algorithm>
#include <cmath>
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
    current=Frame{}; history.clear(); explored.clear(); obstacles.clear(); resources.clear(); enemies.clear(); visits.clear(); events.clear(); spawn.reset();
  } else if (!history.empty() && f.tick<=current.tick) return false;
  if (!history.empty() && (f.width!=current.width || f.height!=current.height || f.team!=current.team))
    throw std::invalid_argument("map/team changed within a session");
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
    if(visible(it->second.pos) && !now_resources.count(it->first)) it=resources.erase(it); else ++it;
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

Action Planner::decide(const World &w) {
  const auto &f=w.current; reason="wait";
  if(!f.alive || f.tick>=max_ticks_) return {};
  for(const auto &e:w.events.items) if(e.kind=="world_split" && e.start==f.tick && w.split_line(f.pos,e)) {
    reason="forced_respawn"; return {};
  }
  const double hp=f.hp-w.environmental_damage(f.pos,f.tick,f.protected_spawn);
  if(hp<=0) {reason="pre_combat_event_death";return {};}
  const int size=f.width*f.height, root=f.pos.y*f.width+f.pos.x;
  const double inf=std::numeric_limits<double>::infinity();
  std::vector<double> costs(size,inf); std::vector<int> steps(size,0), first(size,-1);
  using Entry=std::pair<double,int>; std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
  costs[root]=0; queue.push({0,root});
  while(!queue.empty()) {
    auto [cost,index]=queue.top();queue.pop(); if(cost>costs[index]+1e-6) continue;
    Pos p{index%f.width,index/f.width};
    for(int d=0;d<4;++d) {
      Pos q=add(p,delta[d]); int arrival=f.tick+steps[index]+1;
      if(w.blocked(q,arrival-1) || w.occupied(q)) continue;
      int next=q.y*f.width+q.x;
      double extra=1+(w.explored.count(q)?0:.35);
      extra+=std::min(50.0,w.environmental_damage(q,arrival,false)*.15);
      if(stage_==2) for(const auto &v:f.entities)
        if(v.type=="robot" && !v.protected_spawn && aligned(q,v.pos)) extra+=.6;
      double nc=cost+extra;
      if(nc+1e-6<costs[next]) {costs[next]=nc;steps[next]=steps[index]+1;first[next]=(index==root?d:first[index]);queue.push({nc,next});}
    }
  }
  double best_goal=-1; std::optional<Pos> goal; std::string goal_type="explore";
  auto offer=[&](Pos p,double reward,const std::string &why) {
    if(!w.inside(p)) return;
    int i=p.y*f.width+p.x;
    if(!std::isfinite(costs[i]) || steps[i]>max_ticks_-f.tick) return;
    double value=reward/std::pow(costs[i]+1,.85);
    if(target_ && p==*target_) value*=1.12;
    if(value>best_goal) {best_goal=value;goal=p;goal_type=why;}
  };
  bool need_normal=false, need_high=false;
  if(stage_==2) for(const auto &m:f.missions) if(!f.completed.count(m["id"].get<std::string>())) {
    auto type=m["type"].get<std::string>();
    if(type=="normal_resources") need_normal=true;
    if(type=="high_resources") need_high=true;
    if(type=="reach") offer({m["position"][0],m["position"][1]},6,"mission");
  }
  for(const auto &[id,r]:w.resources) {
    (void)id;
    double reward=stage_==1?r.value*.2:std::min(r.value,std::max(0.0,120-f.resource_value))*.25;
    if(stage_==2 && ((r.value>=20 && need_high) || (r.value<20 && need_normal))) reward+=5;
    offer(r.pos,std::max(.2,reward),"resource");
  }
  if(stage_==2) for(const auto &e:w.events.items) if(e.end>f.tick) {
    if(e.kind=="angel" && f.hp<80) for(auto p:e.points) {
      int i=p.y*f.width+p.x;
      if(f.tick+steps[i]>=e.start && f.tick+steps[i]<e.end) offer(p,(100-f.hp)*.045,"heal");
    }
    if(e.kind=="unrivaled" && e.rect && !w.effect("unrivaled",f.team,f.tick))
      for(int x=(*e.rect)[0].x;x<=(*e.rect)[1].x;++x) for(int y=(*e.rect)[0].y;y<=(*e.rect)[1].y;++y) {
        Pos p{x,y};int i=y*f.width+x;
        if(f.tick+steps[i]+e.number("stay_ticks",10)<e.end) offer(p,5,"fortress");
      }
  }
  for(int i=0;i<size;++i) {
    if(i==root || !std::isfinite(costs[i])) continue;
    Pos p{i%f.width,i/f.width}; int gain=0;
    for(int x=std::max(0,p.x-f.radius);x<=std::min(f.width-1,p.x+f.radius);++x)
      for(int y=std::max(0,p.y-f.radius);y<=std::min(f.height-1,p.y+f.radius);++y) if(!w.explored.count({x,y})) ++gain;
    if(gain) offer(p,(stage_==1?2.3:1.3)*std::min(1.0,gain/20.0),"explore");
  }
  if(!goal && stage_==2) {
    for(const auto &[team,record]:w.enemies) { (void)team;if(f.tick-record.second<20) offer(record.first.pos,2,"pursue"); }
    if(!goal) for(int i=0;i<size;++i) {
      Pos p{i%f.width,i/f.width};if(i==root) continue;
      auto visit=w.visits.find(p);offer(p,1.0/(1+(visit==w.visits.end()?0:visit->second)),"patrol");
    }
  }
  target_=goal; int preferred=-1;
  if(goal) preferred=first[goal->y*f.width+goal->x];
  const bool reverse=w.effect("illusion",f.team,f.tick), self_unrivaled=w.effect("unrivaled",f.team,f.tick);
  const bool purity=w.global("purity",f.tick);
  double reflection=20;
  for(const auto &e:w.events.items) if(e.kind=="purity" && e.active(f.tick)) reflection=e.number("reflection_damage",20);
  double incoming=0;
  if(stage_==2 && !f.protected_spawn) for(const auto &v:f.entities) if(v.type=="robot" && !v.protected_spawn) {
    bool un=w.effect("unrivaled",v.team,f.tick);
    int radius=static_cast<int>(w.effect_number("unrivaled",v.team,f.tick,"attack_radius",2));
    if((un && std::max(std::abs(v.pos.x-f.pos.x),std::abs(v.pos.y-f.pos.y))<=radius) || (!un && aligned(v.pos,f.pos)))
      incoming+=un?w.effect_number("unrivaled",v.team,f.tick,"attack_damage",50):20;
  }
  const double vulnerable=w.effect("pan",f.team,f.tick)?w.effect_number("pan",f.team,f.tick,"damage_multiplier",2):1;
  std::vector<Action> actions{{"wait",""}};
  if(stage_==2) {actions.push_back({"defend",""});if(!f.protected_spawn) for(const auto &d:dirs) actions.push_back({"attack",d});}
  for(const auto &d:dirs) actions.push_back({"move",d});
  Action best; double best_score=-inf;
  for(const auto &a:actions) {
    double score=a.type=="wait"?-.3:-.15, received=incoming*vulnerable;
    Pos end=f.pos; int actual=-1;
    if(a.type=="move") {
      int command=std::find(dirs.begin(),dirs.end(),a.direction)-dirs.begin();actual=reverse?(command^1):command;
      end=add(f.pos,delta[actual]);if(w.blocked(end,f.tick) || w.occupied(end)) continue;
      score=.1+(actual==preferred?.6:0);
      if(goal) score+=.12*(distance(f.pos,*goal)-distance(end,*goal));
      auto visit=w.visits.find(end);if(visit!=w.visits.end()) score-=std::min(.5,visit->second*.025);
      // The next activation occurs before the next move: leave danger one Tick early.
      score-=std::min(100.0,w.environmental_damage(end,f.tick+1,false)*.15);
      if(stage_==2) for(const auto &e:w.events.items) if(e.active(f.tick)) {
        if(e.kind=="angel" && e.contains(end)) score+=std::min(100-f.hp,e.number("heal_per_tick",10)*e.number("effect_ticks",10))*.025;
        if(e.kind=="pepper") for(auto point:e.points) if(point!=end && (point.x==end.x || point.y==end.y))
          for(const auto &v:f.entities) if(v.type=="robot" && distance(v.pos,point)==1) score-=e.number("damage",50)*.035;
      }
    } else if(a.type=="defend") {
      received=0;
      if(!purity && !f.protected_spawn) for(const auto &v:f.entities) if(v.type=="robot" && !v.protected_spawn) {
        bool un=w.effect("unrivaled",v.team,f.tick);
        int radius=static_cast<int>(w.effect_number("unrivaled",v.team,f.tick,"attack_radius",2));
        if((un && std::max(std::abs(v.pos.x-f.pos.x),std::abs(v.pos.y-f.pos.y))<=radius) || (!un && aligned(v.pos,f.pos)))
          received+=(un?w.effect_number("unrivaled",v.team,f.tick,"defended_damage",10):10)*vulnerable;
      }
    } else if(a.type=="attack") {
      std::vector<const Entity*> targets; int dir=std::find(dirs.begin(),dirs.end(),a.direction)-dirs.begin();
      for(const auto &v:f.entities) if(v.type=="robot") {
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
        score+=std::min(v->hp,damage)*(f.effective_damage<300?.065:.012);
        if(damage>=v->hp) score+=.9;
        if(purity) received+=defend_probability*reflection;
      }
    }
    // Being in the fortress after combat counts this Tick; acquisition helps next Tick.
    if(stage_==2 && a.type!="attack" && !self_unrivaled) for(const auto &e:w.events.items)
      if(e.kind=="unrivaled" && e.active(f.tick) && e.contains(end) && e.end-f.tick>=e.number("stay_ticks",10)) score+=.9;
    score-=received*.045;
    if(received>=hp) score-=3;
    if(a.type!="move") score-=std::min(100.0,w.environmental_damage(end,f.tick+1,f.protected_spawn)*.15);
    if(stage_==1 && goal && *goal==f.pos && a.type=="wait") score=2;
    if(score>best_score) {best_score=score;best=a;}
  }
  reason=goal_type;
  if(best.type=="attack" || best.type=="defend") reason="combat";
  if(!legal(best,f,stage_)) return {};
  return best;
}
} // namespace rmtc
