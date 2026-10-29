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
Json event_json(const Event &e,bool versioned=false) {
  Json r{{"kind",e.kind},{"start_tick",e.start},{"end_tick",e.end},{"params",e.params},{"points",Json::array()}};
  for(auto p:e.points) r["points"].push_back({p.x,p.y});
  if(e.rect) r["rect"]={{(*e.rect)[0].x,(*e.rect)[0].y},{(*e.rect)[1].x,(*e.rect)[1].y}};
  if(versioned) {r["event_id"]=e.id;r["revision"]=e.revision;}
  return r;
}
} // namespace

int distance(Pos a, Pos b) { return std::abs(a.x-b.x)+std::abs(a.y-b.y); }
double combat_score(double adjusted_damage) { return 15*std::clamp(adjusted_damage,0.0,500.0)/500; }
double projected_damage(double previous,double attack,double reward,int deaths) {
  return std::max(0.0,previous+attack+reward-60*deaths);
}
std::string model_schema_error_code(const std::exception &error) {
  if(auto json_error=dynamic_cast<const Json::exception*>(&error)) {
    if(json_error->id==302) return "json_type";
    if(json_error->id==403) return "json_missing_field";
    return "json_schema_error";
  }
  static const std::map<std::string,std::string> codes{
    {"invalid model schema or session","schema_or_session"},{"model batch too large","batch_limit"},
    {"unknown event revision","event_revision_identity"},{"stale event revision","event_revision_stale"},
    {"event revision kind mismatch","event_revision_kind"},{"duplicate event revision","event_revision_duplicate"},
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
    // Optional diagnostic fields do not make legacy frames invalid. Unknown
    // or malformed values provide no evidence for the recovery experiment.
    if(a.contains("action") && a["action"].is_string() && a["action"].get_ref<const std::string&>().size()<=16)
      f.action_type=a["action"].get<std::string>();
    if(a.contains("error") && a["error"].is_string() && a["error"].get_ref<const std::string&>().size()<=32)
      f.action_error=a["error"].get<std::string>();
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
void Events::clear() { items.clear(); effects.clear(); next_id_=0; }
Json Events::merge(const Json &j, const Frame &f,const Json *request_context) {
  if (!j.is_object() || j.at("session_id")!=f.session || !j.at("events").is_array() || !j.at("effects").is_array())
    throw std::invalid_argument("invalid model schema or session");
  if (j["events"].size()>64 || j["effects"].size()>128) throw std::invalid_argument("model batch too large");
  auto new_items=items; auto new_effects=effects;int next_id=next_id_;
  Json updates=Json::array();std::set<std::string> revised;
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
    if(request_context && v.contains("event_id") && !v["event_id"].is_null()) {
      auto id=string(v["event_id"],"event_id",64);
      int revision=integer(v.at("revision"),"event revision",0,1000000);
      const auto &known=request_context->at("events");
      auto requested=std::find_if(known.begin(),known.end(),[&](const Json &old){
        return old.value("event_id",std::string{})==id;
      });
      auto existing=std::find_if(new_items.begin(),new_items.end(),[&](const Event &old){return old.id==id;});
      if(id.empty() || requested==known.end() || existing==new_items.end())
        throw std::invalid_argument("unknown event revision");
      if(!revised.insert(id).second) throw std::invalid_argument("duplicate event revision");
      if(requested->at("revision")!=revision || existing->revision!=revision)
        throw std::invalid_argument("stale event revision");
      if(existing->kind!=e.kind) throw std::invalid_argument("event revision kind mismatch");
      e.id=id;e.revision=revision;
      if(event_json(*existing)!=event_json(e)) {
        ++e.revision;
        updates.push_back({{"kind","event_revision"},{"event_id",id},
                           {"before",event_json(*existing,true)},{"after",event_json(e,true)}});
        *existing=e;
      }
      continue;
    }
    auto it=std::find_if(new_items.begin(),new_items.end(),[&](const Event &old){
      return old.kind==e.kind && old.start==e.start && old.rect==e.rect && old.points==e.points;
    });
    if(it==new_items.end()) {
      e.id="event-"+std::to_string(next_id++);
      if(request_context) for(const auto &old:new_items)
        if(old.kind==e.kind && old.start==e.start)
          updates.push_back({{"kind","event_identity_ambiguous"},{"existing",event_json(old,true)},
                             {"incoming",event_json(e,true)}});
      new_items.push_back(e);
    } else {
      e.id=it->id;e.revision=it->revision;
      if(request_context && event_json(*it)!=event_json(e)) {
        // Preserve the legacy exact-geometry update path for parameters/end
        // time. Geometry or activation corrections still require an explicit
        // model reference; same kind/start alone never identifies an event.
        const auto &known=request_context->at("events");
        auto requested=std::find_if(known.begin(),known.end(),[&](const Json &old){
          return old.value("event_id",std::string{})==it->id;
        });
        if(requested!=known.end() && requested->at("revision")!=it->revision)
          throw std::invalid_argument("stale event revision");
        ++e.revision;
        updates.push_back({{"kind","event_revision"},{"identity_basis","exact_geometry"},
          {"event_id",e.id},{"before",event_json(*it,true)},{"after",event_json(e,true)}});
      }
      *it=e;
    }
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
  items=std::move(new_items); effects=std::move(new_effects);next_id_=next_id;
  return updates;
}
Json Events::context(bool versioned) const {
  Json out{{"events",Json::array()},{"effects",Json::array()}};
  auto begin=items.size()>32?items.size()-32:0;
  for (size_t i=begin;i<items.size();++i) {
    out["events"].push_back(event_json(items[i],versioned));
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

double resource_arrival_probability(const World &w,Pos resource,int arrival) {
  double available=1;
  for(const auto &[team,record]:w.enemies) {
    int age=w.current.tick-record.second;
    if(age<0 || age>4 || record.first.hp<=0) continue;
    const auto *past=w.at(record.second-1);
    std::optional<Pos> before;
    if(past) for(const auto &e:past->entities) if(e.type=="robot" && e.team==team) before=e.pos;
    double total=.5,target=0; // An outside option preserves attack/wait/other intent.
    for(const auto &[id,r]:w.resources) {
      (void)id;int dist=distance(record.first.pos,r.pos);
      double weight=(r.value/10.0)/(dist+1.0);
      if(before && distance(*before,record.first.pos)==1) {
        int progress=distance(*before,r.pos)-dist;
        weight*=progress>0?3.0:.5;
      }
      total+=weight;if(r.pos==resource) target+=weight;
    }
    int earliest=std::max(0,distance(record.first.pos,resource)-age);
    double urgency=std::clamp((arrival-earliest)/4.0,0.0,1.0);
    // Each rival's intent mass is shared across its alternatives, rather than
    // assuming it simultaneously rushes every resource. Unknown plans remain uncertain.
    available*=1-.9*(target/total)*urgency/(1+.25*age);
  }
  return std::clamp(available,.1,1.0);
}

Planner::Planner(int stage,int max_ticks,PlannerConfig config):stage_(stage),max_ticks_(max_ticks),config_(config) {
  if(!std::isfinite(config_.early_movement_scale) || config_.early_movement_scale<.1 || config_.early_movement_scale>5
    || !std::isfinite(config_.early_damage_scale) || config_.early_damage_scale<0 || config_.early_damage_scale>1
    || config_.damage_scale_ticks< -1 || config_.damage_scale_ticks>max_ticks_
    || config_.forecast_start_tick< -1 || config_.forecast_start_tick>max_ticks_
    || config_.resource_lookahead<1 || config_.resource_lookahead>3
    || config_.damage_reserve<0 || config_.damage_reserve>600)
    throw std::invalid_argument("invalid planner configuration");
}
Action Planner::decide(const World &observed) {
  // A development comparison may still make real, validated AI calls while
  // withholding only their interpretation from action selection. Do not alter
  // the original memory or the next model request's validated context.
  std::optional<World> without_interpretation;
  if(!config_.use_event_interpretation) {
    without_interpretation=observed;without_interpretation->events.clear();
  }
  const World &w=without_interpretation?*without_interpretation:observed;
  const auto &f=w.current; reason="wait";goal_reason.clear();purity_evaluations=Json::array();opening_route_evaluation=Json();resource_route_evaluation=Json();resource_race_evaluations=Json::array();resource_recovery_evaluation=Json();
  observation_evaluation=Json();
  if(observation_session_!=f.session) {
    observation_session_=f.session;observation_plan_={};observation_attempts_.clear();
  }
  if(!f.alive || f.tick>=max_ticks_) {recovery_mission_.reset();resource_feedback_={};return {};}
  for(const auto &e:w.events.items) if(e.kind=="world_split" && e.start==f.tick && w.split_line(f.pos,e)) {
    reason="forced_respawn"; return {};
  }
  const double hp=f.hp-w.environmental_damage(f.pos,f.tick,f.protected_spawn);
  if(hp<=0) {reason="pre_combat_event_death";return {};}
  std::map<int,double> unavoidable_event_damage;
  auto event_cost=[&](Pos p,int tick,bool protected_spawn) {
    double damage=w.environmental_damage(p,tick,protected_spawn);
    auto unavoidable=unavoidable_event_damage.find(tick);
    if(unavoidable!=unavoidable_event_damage.end()) damage=std::max(0.0,damage-unavoidable->second);
    if(damage<=0) return 0.0;
    if(damage>=hp) return 100.0;
    // HP has no direct score value. A survivable event that leaves a healthy
    // reserve should not forfeit an irreplaceable resource or a task. Keep a
    // large penalty for lethal events and dangerously low resulting health.
    return damage*(hp-damage>=50?.008:.15);
  };
  const bool forecast=config_.tactical_forecast && stage_==2
    && f.tick>=(config_.forecast_start_tick<0?max_ticks_/5:config_.forecast_start_tick);
  const bool resource_recovery=config_.resource_blocked_recovery && stage_==2 && f.tick<max_ticks_/5
    && !forecast && !config_.resource_beam && !config_.information_paths
    && !config_.opening_route_information && !config_.resource_route_pickups
    && !config_.resource_race_yield && !config_.resource_fresh_returns
    && w.events.items.empty() && w.events.effects.empty();
  auto &feedback=resource_feedback_;
  if(!resource_recovery) feedback={};
  else {
    if(feedback.session!=f.session || f.tick<feedback.processed_tick) feedback={};
    feedback.session=f.session;
    if(feedback.processed_tick!=f.tick) {
      const bool contiguous=feedback.proposed_tick==f.tick-1 && f.action_tick==f.tick-1;
      const bool failed=contiguous && feedback.action.type=="move" && f.action_type=="move"
        && !f.moved && f.action_error=="E_BLOCKED" && f.pos==feedback.from && feedback.proposed_entry;
      if(failed) {
        feedback.failures=feedback.failed_entry==feedback.proposed_entry?feedback.failures+1:1;
        feedback.failed_entry=feedback.proposed_entry;
      } else {feedback.failures=0;feedback.failed_entry.reset();}
      if(feedback.alternative && (!contiguous || (feedback.action.type=="move" && !f.moved))) {
        resource_recovery_evaluation={{"status","cancelled"},{"cause","route_receipt"}};
        feedback.alternative.reset();feedback.blocked_entry.reset();
      }
      feedback.processed_tick=f.tick;
    }
  }
  const int size=f.width*f.height, root=f.pos.y*f.width+f.pos.x;
  std::vector<double> escape_damage;
  if(stage_==2 && config_.event_escape) {
    int horizon=f.tick;
    for(const auto &e:w.events.items)
      if((e.kind=="explosion" || e.kind=="world_split") && e.start>f.tick
        && e.start<=f.tick+32 && e.start<max_ticks_) horizon=std::max(horizon,e.start);
    if(horizon>f.tick) {
      if(config_.event_unavoidable_cost && !f.protected_spawn) for(const auto &e:w.events.items)
        if((e.kind=="explosion" || e.kind=="world_split") && e.start>f.tick && e.start<=horizon
          && !unavoidable_event_damage.count(e.start)) {
          // Even ignoring walls, every cell reachable before this activation
          // may take the same minimum damage. That fixed loss should not bias
          // which frontier/resource we choose. Ignore walls here deliberately:
          // this is a lower bound, never an invented escape route.
          double minimum=std::numeric_limits<double>::infinity();
          for(int i=0;i<size;++i) {
            Pos p{i%f.width,i/f.width};
            if(distance(f.pos,p)<=e.start-f.tick)
              minimum=std::min(minimum,w.environmental_damage(p,e.start,false));
          }
          unavoidable_event_damage[e.start]=minimum;
        }
      // Backward time layers: an action in t reaches its cell before events in
      // t+1. Keep the least future event damage after each current action,
      // rather than waiting until the last step to notice a wide danger zone.
      // Future routes use remembered walls and validated event geometry only.
      // Future robot occupancy is unknown; actual first steps are checked below.
      escape_damage.resize(size);std::vector<double> previous(size);
      for(int i=0;i<size;++i) escape_damage[i]=w.environmental_damage({i%f.width,i/f.width},horizon,false);
      for(int tick=horizon-1;tick>f.tick;--tick) {
        for(int i=0;i<size;++i) {
          Pos p{i%f.width,i/f.width};double best=escape_damage[i]; // Waiting is allowed.
          for(auto d:delta) {
            Pos q=add(p,d);
            if(!w.blocked(q,tick)) best=std::min(best,escape_damage[q.y*f.width+q.x]);
          }
          previous[i]=w.environmental_damage(p,tick,false)+best;
        }
        escape_damage.swap(previous);
      }
    }
  }
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
  const bool opening_information=config_.opening_route_information && stage_==2
    && f.tick<std::min(60,max_ticks_/5) && w.high_collected==0
    && !config_.information_paths && !config_.resource_beam
    && w.events.items.empty() && w.events.effects.empty()
    && std::none_of(w.resources.begin(),w.resources.end(),[](const auto &r){return r.second.value>=20;})
    && std::none_of(f.missions.begin(),f.missions.end(),[&](const auto &m) {
      return m["type"]=="high_resources" && f.completed.count(m["id"].template get<std::string>());
    });
  // Preserve the baseline path costs, lengths and goal ordering. This mask
  // records only alternative first steps reaching a cell at the SAME cost
  // and Tick count. Unknown terrain remains a traversability hypothesis.
  std::vector<unsigned char> equal_first(opening_information?size:0,0);
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
      if(nc+1e-6<costs[next]) {
        costs[next]=nc;steps[next]=steps[index]+1;first[next]=(index==root?d:first[index]);
        if(opening_information) equal_first[next]=index==root?(1<<d):equal_first[index];
        queue.push({nc,next});
      } else if(opening_information && std::abs(nc-costs[next])<1e-6 && steps[next]==steps[index]+1) {
        unsigned char mask=index==root?(1<<d):equal_first[index];
        if(mask & ~equal_first[next]) {
          equal_first[next]|=mask;queue.push({costs[next],next});
        }
      }
    }
  }
  double best_goal=-1; std::optional<Pos> goal; std::string goal_type="explore";
  bool defer_hunt=false;
  auto offer=[&](Pos p,double reward,const std::string &why,double extra_ticks=0) {
    if(defer_hunt && (why=="engage" || why=="ambush" || why=="pursue")) return;
    if(!w.inside(p)) return;
    int i=p.y*f.width+p.x;
    if(!std::isfinite(costs[i]) || steps[i]+extra_ticks>max_ticks_-f.tick) return;
    double value=reward/std::pow(costs[i]+extra_ticks+1,.85);
    if(config_.frontier_distance_window && stage_==2 && why=="explore"
      && w.resources.empty() && f.tick<max_ticks_/5) {
      // A nearby thin strip competes with a full new footprint farther away.
      // Compare them over a travel window derived from our actual vision,
      // keeping the original scale at one vision radius of travel. This only
      // changes hypothetical exploration value, never observed map contents.
      const double window=2*f.radius,reference=f.radius+1;
      value=reward*std::pow((reference+window)/reference,.85)
        /std::pow(costs[i]+extra_ticks+window+1,.85);
    }
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
      // Only public unfinished tasks and our own remembered route are used.
      // This defers optional pursuit goals, never immediate shots or defense.
      if(config_.coordinate_before_hunt && f.tick>max_ticks_/3 && w.resources.empty()
        && f.effective_damage>=500+config_.damage_reserve
        && w.explored.size()>=static_cast<size_t>(size*.75) && w.inside(p)) {
        int index=p.y*f.width+p.x;
        if(std::isfinite(costs[index]) && steps[index]<=remaining) defer_hunt=true;
      }
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
  const bool competitive_exploration=config_.competitive_exploration && stage_==2
    && f.tick<max_ticks_/5 && w.resources.empty();
  if(stage_==2 && (config_.race_aware_resources || competitive_exploration)) for(const auto &[team,record]:w.enemies) {
    int age=f.tick-record.second;if(age>4) continue;
    if(!config_.race_aware_resources && (age!=0 || record.first.hp<=0)) continue;
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
  std::vector<double> exploration_weights;
  if(competitive_exploration) {
    exploration_weights.assign(size,1);
    for(int i=0;i<size;++i) {
      Pos p{i%f.width,i/f.width};
      if(w.explored.count(p) || !std::isfinite(costs[i])) continue;
      for(const auto &rival:rivals) if(rival.age==0 && rival.steps[i]>=0
        && rival.steps[i]<=2*f.radius && rival.steps[i]+2<steps[i]) {
        // Earlier access is only an opportunity estimate. The rival may
        // choose a different route; do not mark cells seen or resources gone.
        exploration_weights[i]=.65;break;
      }
    }
  }
  auto exploration_weight=[&](int x,int y) {
    return exploration_weights.empty()?1.0:exploration_weights[y*f.width+x];
  };
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
  auto denial_reward=[&](Pos resource,double value,int arrival) {
    if(!config_.contested_task_resources || stage_!=2 || arrival<0 || arrival>6
      || f.tick+arrival>=max_ticks_/5) return 0.0;
    // A reachable own path beats even the rival's optimistic Manhattan lower
    // bound. Current visible rivals only: neither their task progress nor their
    // intended route is known. A small lead makes this an opportunity to test,
    // not a prediction that the rival would actually collect this resource.
    bool contested=false;
    for(const auto &[team,record]:w.enemies) {
      (void)team;if(record.second!=f.tick) continue;
      int earliest=distance(record.first.pos,resource);
      if(earliest>arrival && earliest<=arrival+4) {contested=true;break;}
    }
    if(!contested) return 0.0;
    double reward=0;
    for(const auto &m:f.missions) {
      auto type=m["type"].get<std::string>();double target=std::max(1.0,m.value("target",1.0));
      double increment=type=="normal_resources"?(value<20?1:0)
        :(type=="high_resources"?(value>=20?1:0):(type=="resource_value"?value:0));
      // Spread possible denied task points over three rivals. This soft
      // opportunity value is never stored as any team's actual score.
      reward+=5.0/3*std::min(1.0,increment/target);
    }
    return reward;
  };
  struct ResourceOption {Pos pos;double reward,cost,value,task_bonus,denial_bonus,probability,competition;int steps,seen;};
  std::vector<ResourceOption> resource_options;
  for(const auto &[id,r]:w.resources) {
    (void)id;
    double reward=stage_==1?r.value*.2:std::min(r.value,std::max(0.0,120-f.resource_value))*.25;
    double task_bonus=stage_==2?task_reward(r.value,0,0,0):0;
    reward+=task_bonus;
    if(stage_==2 && w.spawn && f.tick<40 && r.value>=20) {
      const bool same_half_x=(r.pos.x<f.width/2)==(w.spawn->x<f.width/2);
      const bool same_half_y=(r.pos.y<f.height/2)==(w.spawn->y<f.height/2);
      // candidate: prefer 20R high-value resources in our own quadrant during
      // the opening, so a visible high-value is not deferred behind two normal
      // resource missions and then lost to an opponent.
      if(same_half_x && same_half_y) reward+=6.0;
    }
    double denial_bonus=r.seen>=f.tick-1?denial_reward(r.pos,r.value,steps[r.pos.y*f.width+r.pos.x]):0;
    reward+=denial_bonus;
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
      && distance(*w.spawn,r.pos)+3<distance(*w.spawn,f.pos)) {
      // A fresh, visible resource has evidence of continued availability.
      // Compare its full ordinary score and route cost before choosing an
      // unknown frontier. This never assumes the way back is safe or empty.
      const bool fresh_return=config_.resource_fresh_returns && f.tick<std::min(40,max_ticks_/5)
        && r.seen==f.tick && !forecast && w.events.items.empty() && w.events.effects.empty();
      return_priority=fresh_return?1:.6;
    }
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
    else if(stage_==2 && config_.resource_arrival_value)
      competition=resource_arrival_probability(w,r.pos,steps[r.pos.y*f.width+r.pos.x]);
    if(config_.resource_race_yield && stage_==2 && f.tick<std::min(40,max_ticks_/5)
      && !forecast && !config_.race_aware_resources && w.events.items.empty() && w.events.effects.empty()
      && r.seen==f.tick && distance(f.pos,r.pos)>=2 && !w.blocked(r.pos,f.tick) && !w.occupied(r.pos)) {
      const auto *previous=w.at(f.tick-1);
      if(previous && previous->alive) for(const auto &enemy:f.entities)
        if(enemy.type=="robot" && enemy.hp>0 && distance(enemy.pos,r.pos)==1) {
          auto before=std::find_if(previous->entities.begin(),previous->entities.end(),[&](const auto &old) {
            return old.type=="robot" && old.team==enemy.team && old.hp>0;
          });
          if(before==previous->entities.end() || distance(before->pos,enemy.pos)!=1
            || distance(before->pos,r.pos)!=2) continue;
          // Preserve a possible immediate kill that can cancel their move.
          if(!f.protected_spawn && !enemy.protected_spawn && enemy.hp<=20
            && normal_ray_clear(f.pos,enemy.pos,f.team,f.entities)) continue;
          const double prior=competition;
          // They may stop, attack, or change direction: retain value, rather
          // than deleting the resource or writing a guessed pickup to World.
          competition=std::min(competition,.1);
          resource_race_evaluations.push_back({{"resource",{r.pos.x,r.pos.y}},{"value",r.value},
            {"enemy",enemy.team},{"enemy_before",{before->pos.x,before->pos.y}},
            {"enemy_now",{enemy.pos.x,enemy.pos.y}},{"enemy_arrival_lower_bound",1},
            {"own_arrival_lower_bound",distance(f.pos,r.pos)},
            {"previous_discount",prior},{"approach_discount",competition}});
          break;
        }
    }
    double probability=confidence*competition*return_priority;
    double expected=std::max(.1,reward)*probability;
    offer(r.pos,expected,"resource");
    double cost=costs[r.pos.y*f.width+r.pos.x];
    if(stage_==2 && std::isfinite(cost) && steps[r.pos.y*f.width+r.pos.x]<=max_ticks_-f.tick)
      resource_options.push_back({r.pos,expected,cost,r.value,task_bonus,denial_bonus,probability,competition,steps[r.pos.y*f.width+r.pos.x],r.seen});
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
    const int probability_horizon=f.width+f.height+4;
    std::vector<int> probability_index(size,-1);
    std::vector<std::vector<double>> probabilities;
    if(config_.resource_arrival_value && !config_.race_aware_resources) {
      probabilities.assign(resource_options.size(),std::vector<double>(probability_horizon+1));
      for(size_t i=0;i<resource_options.size();++i) {
        Pos p=resource_options[i].pos;probability_index[p.y*f.width+p.x]=static_cast<int>(i);
        for(int t=0;t<=probability_horizon;++t) probabilities[i][t]=resource_arrival_probability(w,p,t);
      }
    }
    auto arrival_probability=[&](Pos p,int arrival) {
      return probabilities[probability_index[p.y*f.width+p.x]][std::clamp(arrival,0,probability_horizon)];
    };
    auto follow_reward=[&](const ResourceOption &r,int normal,int high,double collected_value,int arrival) {
      double first_points=std::min(r.value,std::max(0.0,120-f.resource_value))*.25;
      double future_points=std::min(r.value,std::max(0.0,120-f.resource_value-collected_value))*.25;
      double reward=std::max(0.0,r.reward+(task_reward(r.value,normal,high,collected_value)-r.task_bonus
        +future_points-first_points+((r.seen>=f.tick-1?denial_reward(r.pos,r.value,arrival):0)-r.denial_bonus))*r.probability);
      if(config_.race_aware_resources) reward*=capture_probability(r.pos,arrival)/r.competition;
      else if(config_.resource_arrival_value)
        reward*=arrival_probability(r.pos,arrival)/r.competition;
      return reward;
    };
    auto delayed_loss=[&](size_t i,size_t j,size_t k,int arrival) {
      if(!config_.resource_arrival_value || config_.race_aware_resources) return 0.0;
      size_t end=k<resource_options.size()?k:j;double loss=0;
      for(size_t m=0;m<resource_options.size();++m) if(m!=i && m!=j && m!=k && pair_steps[end][m]>=0) {
        const auto &r=resource_options[m];
        double now=arrival_probability(r.pos,r.steps);
        double later=arrival_probability(r.pos,arrival+pair_steps[end][m]);
        double deferred=(r.reward/r.competition)*std::max(0.0,now-later)/std::pow(r.cost+1,.85);
        loss=std::max(loss,deferred);
      }
      return .12*loss;
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
        value=std::max(value,second-delayed_loss(i,j,resource_options.size(),arrival));
        if(config_.resource_lookahead<3) continue;
        for(size_t k=0;k<resource_options.size();++k) if(k!=i && k!=j) {
          if(pair_steps[j][k]<0) continue;
          const auto &c=resource_options[k];double end=elapsed+pair_steps[j][k];
          if(end<=max_ticks_-f.tick) value=std::max(value,second+.4*follow_reward(c,
            normal+(b.value<20?1:0),high+(b.value>=20?1:0),a.value+b.value,arrival+pair_steps[j][k])/std::pow(end+1,.85)
            -delayed_loss(i,j,k,arrival+pair_steps[j][k]));
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
        if(!config_.event_timing && !config_.fortress_time_budget) {
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
    Pos p{i%f.width,i/f.width}; double gain=0;
    for(int x=std::max(0,p.x-f.radius);x<=std::min(f.width-1,p.x+f.radius);++x)
      for(int y=std::max(0,p.y-f.radius);y<=std::min(f.height-1,p.y+f.radius);++y)
        if(!w.explored.count({x,y})) gain+=exploration_weight(x,y);
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
        double novel=0;
        for(int x=std::max(0,b.pos.x-f.radius);x<=std::min(f.width-1,b.pos.x+f.radius);++x)
          for(int y=std::max(0,b.pos.y-f.radius);y<=std::min(f.height-1,b.pos.y+f.radius);++y)
            if(!w.explored.count({x,y}) && (std::abs(x-a.pos.x)>f.radius || std::abs(y-a.pos.y)>f.radius))
              novel+=exploration_weight(x,y);
        value=std::max(value,a.reward/std::pow(a.cost+1,.85)+.55*1.3*novel/20.0/std::pow(elapsed+1,.85));
      }
      offer(a.pos,value*std::pow(a.cost+1,.85),"explore");
    }
  }
  if(config_.respawn_task_route && stage_==2 && f.tick>=max_ticks_/3) {
    auto reachable_task=[&](const Json &m) {
      if(m["type"]!="reach" || f.completed.count(m["id"].get<std::string>())) return false;
      Pos p{m["position"][0],m["position"][1]};if(!w.inside(p)) return false;
      int i=p.y*f.width+p.x;
      return std::isfinite(costs[i]) && steps[i]<=max_ticks_-f.tick;
    };
    if(recovery_mission_ && std::none_of(f.missions.begin(),f.missions.end(),[&](const auto &m) {
      return m["id"]==*recovery_mission_ && reachable_task(m);
    })) recovery_mission_.reset();
    const auto *previous=w.at(f.tick-1),*before=w.at(f.tick-32);
    if(!recovery_mission_ && f.protected_spawn && previous && !previous->alive && before
      && w.explored.size()>=static_cast<size_t>(size*.75)) {
      bool contiguous=true;
      for(int tick=f.tick-32;tick<f.tick;++tick) if(!w.at(tick)) {contiguous=false;break;}
      if(contiguous) {
        // Own net combat points include deaths and the cap. This historical
        // rate is a heuristic, not a forecast of enemies or future rewards.
        double combat_rate=std::max(0.0,combat_score(f.effective_damage)-combat_score(before->effective_damage))/32;
        double best_rate=combat_rate;
        for(const auto &m:f.missions) if(reachable_task(m)) {
          Pos p{m["position"][0],m["position"][1]};int i=p.y*f.width+p.x;
          // Do not abandon a long remaining combat window. Start only when
          // delaying this estimated route by one ordinary recovery cycle
          // (three dead windows, next living action) would miss the deadline.
          if(steps[i]+4<max_ticks_-f.tick) continue;
          double rate=5/(costs[i]+1);
          if(rate>best_rate+1e-9) {best_rate=rate;recovery_mission_=m["id"].get<std::string>();}
        }
      }
    }
    // Keep collecting worthwhile known resources and compare immediate combat
    // normally. Only optional hunting goals pause during this public task route.
    if(recovery_mission_) defer_hunt=true;
  }
  // Pursue reachable firing positions, not the enemy's occupied cell. Continue
  // earning legitimate damage after the point cap: it breaks otherwise tied
  // scores. Finite resources and unfinished missions retain their own value.
  auto enemy_position_valid=[&](const std::pair<Entity,int> &record) {
    if(!config_.validated_enemy_memory) return true;
    // A later live observation of the old cell disproves this location. If
    // the enemy were seen again, World would already have refreshed the record.
    // Keep the historical record; do not infer death or a new enemy position.
    for(auto it=w.history.rbegin();it!=w.history.rend() && it->tick>record.second;++it)
      if(it->alive && std::abs(it->pos.x-record.first.pos.x)<=it->radius
        && std::abs(it->pos.y-record.first.pos.y)<=it->radius) return false;
    return true;
  };
  if(stage_==2) {
    // Crossing 75% explored is not evidence that the remaining reachable
    // frontier has no resources. During the finite-resource window, retain
    // an already selected exploration plan; immediate combat is scored below.
    const bool finish_exploring=config_.finish_early_exploration && f.tick<max_ticks_/5
      && w.resources.empty() && goal && goal_type=="explore";
    bool hunt=f.tick>max_ticks_/5 || (w.explored.size()>static_cast<size_t>(size*.75) && !finish_exploring);
    for(const auto &[team,record]:w.enemies) {
      if(!enemy_position_valid(record)) continue;
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
    for(const auto &[team,record]:w.enemies) {
      (void)team;
      if(f.tick-record.second<20 && enemy_position_valid(record)) offer(record.first.pos,2,"pursue");
    }
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
  target_=goal;target_type_=goal_type;goal_reason=goal?goal_type:"";int preferred=-1;
  if(goal) preferred=first[goal->y*f.width+goal->x];
  if(searched_first) preferred=*searched_first;
  if(config_.observation_detour && stage_==2) {
    // All traversed cells are observed free cells. The unseen footprint is a
    // reason to look, never a synthetic resource or a guaranteed payoff.
    auto &plan=observation_plan_;
    const bool quiet=f.alive && f.tick<max_ticks_/5 && f.hp>=60 && !forecast
      && w.events.items.empty() && w.events.effects.empty()
      && !config_.information_paths && !config_.resource_beam && !config_.opening_route_information
      && !config_.resource_route_pickups && !feedback.alternative && feedback.failures==0
      && std::none_of(f.entities.begin(),f.entities.end(),[](const auto &e){return e.type=="robot";});
    auto known_free=[&](Pos p) {return w.inside(p) && w.explored.count(p) && !w.blocked(p,f.tick) && !w.occupied(p);};
    auto bfs=[&](Pos origin,std::vector<int> &length,std::vector<int> &direction) {
      length.assign(size,-1);direction.assign(size,-1);std::queue<Pos> q;
      length[origin.y*f.width+origin.x]=0;q.push(origin);
      while(!q.empty()) {
        Pos p=q.front();q.pop();int i=p.y*f.width+p.x;
        for(int d=0;d<4;++d) {
          Pos n=add(p,delta[d]);if(!known_free(n)) continue;int j=n.y*f.width+n.x;
          if(length[j]>=0) continue;
          length[j]=length[i]+1;direction[j]=i==origin.y*f.width+origin.x?d:direction[i];q.push(n);
        }
      }
    };
    std::vector<int> length,direction;
    if(quiet) bfs(f.pos,length,direction);
    auto anchor_present=[&]() {return plan.anchor && std::any_of(w.resources.begin(),w.resources.end(),
      [&](const auto &r){return r.second.pos==*plan.anchor;});};
    bool released=false;
    if(plan.anchor) {
      const bool new_choice=goal && goal_type=="resource" && *goal!=*plan.anchor
        && std::any_of(w.resources.begin(),w.resources.end(),[&](const auto &r) {
          return r.second.pos==*goal && !plan.known_ids.count(r.first);
        });
      const bool interrupted=plan.previous_tick!=f.tick-1
        || (f.action_tick==f.tick-1 && f.action_type=="move" && !f.moved);
      if(!quiet || !anchor_present() || f.tick>plan.deadline || interrupted || new_choice
        || length[plan.anchor->y*f.width+plan.anchor->x]<0) {
        observation_evaluation={{"status",new_choice?"new_resource_selected":"cancelled"}};
        plan={};released=true;
      } else {
        if(plan.waypoint && f.pos==*plan.waypoint) plan.waypoint.reset();
        Pos next=plan.waypoint?*plan.waypoint:*plan.anchor;int i=next.y*f.width+next.x;
        if(length[i]<0) {plan={};released=true;observation_evaluation={{"status","route_invalid"}};}
        else {
          goal=next;goal_type=plan.waypoint?"observe":"resource";preferred=direction[i];
          best_goal=std::max(.7,best_goal);
          observation_evaluation={{"status",plan.waypoint?"observing":"fallback"},
            {"anchor",{plan.anchor->x,plan.anchor->y}},{"deadline",plan.deadline}};
          plan.previous_tick=f.tick;
        }
      }
    }
    const bool needs_high=w.high_collected==0 && std::none_of(w.resources.begin(),w.resources.end(),
      [](const auto &r){return r.second.value>=20;});
    if(!plan.anchor && !released && quiet && needs_high && goal && goal_type=="resource" && f.tick>=config_.observation_min_tick
      && int(w.resources.size())<=config_.observation_max_known_resources
      && !observation_attempts_.count(*goal) && int(observation_attempts_.size())<config_.observation_attempt_limit
      && preferred>=0 && f.radius>=2) {
      const int destination=goal->y*f.width+goal->x;
      auto resource=std::find_if(w.resources.begin(),w.resources.end(),[&](const auto &r) {
        return r.second.pos==*goal && r.second.seen==f.tick && r.second.value<20;
      });
      if(resource!=w.resources.end() && length[destination]>=2 && length[destination]<=8
        && length[destination]==steps[destination]) {
        std::vector<int> remaining,unused;bfs(*goal,remaining,unused);
        std::set<Pos> direct_vision;
        auto footprint=[&](Pos p,auto fn) {
          for(int x=std::max(0,p.x-f.radius);x<=std::min(f.width-1,p.x+f.radius);++x)
            for(int y=std::max(0,p.y-f.radius);y<=std::min(f.height-1,p.y+f.radius);++y) fn(Pos{x,y});
        };
        Pos p=f.pos;int guard=0;
        while(p!=*goal && guard++<=length[destination]) {
          int chosen=-1;
          for(int d=0;d<4;++d) {
            Pos n=add(p,delta[d]);if(!known_free(n)) continue;
            if(remaining[n.y*f.width+n.x]==remaining[p.y*f.width+p.x]-1) {
              if(chosen<0 || (p==f.pos && d==preferred)) chosen=d;
            }
          }
          if(chosen<0) break;
          p=add(p,delta[chosen]);footprint(p,[&](Pos n){direct_vision.insert(n);});
        }
        std::optional<Pos> waypoint;double value=0;int gain=0,extra=0,via=0;
        for(int i=0;i<size;++i) {
          if(length[i]<1 || length[i]>3 || remaining[i]<1 || direction[i]<0) continue;
          int travel=length[i]+remaining[i],detour=travel-length[destination];
          if(detour<0 || detour>config_.observation_extra_limit || f.tick+travel>=max_ticks_) continue;
          Pos q{i%f.width,i/f.width};int novel=0;
          footprint(q,[&](Pos n){if(!w.explored.count(n) && !direct_vision.count(n)) ++novel;});
          if(novel<2*f.radius) continue;
          double rate=double(novel)/(detour+1.0)-.1*length[i];
          if(rate>value) {value=rate;waypoint=q;gain=novel;extra=detour;via=travel;}
        }
        if(waypoint) {
          plan.anchor=goal;plan.waypoint=waypoint;plan.deadline=f.tick+via;plan.previous_tick=f.tick;
          for(const auto &[id,r]:w.resources) {(void)r;plan.known_ids.insert(id);}
          observation_attempts_.insert(*goal);
          observation_evaluation={{"status","started"},{"anchor",{goal->x,goal->y}},
            {"waypoint",{waypoint->x,waypoint->y}},{"new_cells_beyond_direct",gain},
            {"extra_steps",extra},{"fallback_arrival",plan.deadline}};
          goal=waypoint;goal_type="observe";preferred=direction[waypoint->y*f.width+waypoint->x];
        }
      }
    }
    target_=goal;target_type_=goal_type;goal_reason=goal?goal_type:"";
  }
  if(config_.resource_route_pickups && stage_==2 && f.tick<std::min(40,max_ticks_/5)
    && !forecast && !config_.information_paths && !config_.resource_beam && !config_.opening_route_information
    && w.events.items.empty() && w.events.effects.empty()
    && goal && goal_type=="resource" && preferred>=0) {
    // Preserve the selected goal. The reverse search
    // uses the same static edge costs as the forward no-event search. Unlike
    // an information bonus, an incidental pickup must be an observed resource.
    const int destination=goal->y*f.width+goal->x;
    auto exposure=[&](Pos p) {
      double result=0;
      for(const auto &enemy:f.entities) if(enemy.type=="robot" && !enemy.protected_spawn
        && aligned(p,enemy.pos)) result+=.6;
      return result;
    };
    auto edge_cost=[&](Pos p) {
      return 1+(w.explored.count(p)?0:.35)+(w.occupied(p)?1.0:0.0)+exposure(p);
    };
    auto earliest_rival=[&](Pos p) {
      // A hidden car can start in any map cell outside the current square
      // view. Minimize Manhattan distance to that set without assuming its
      // actual position or using obstacles to inflate the lower bound. This
      // covers movement of living cars, not an unobserved future respawn.
      int earliest=f.width+f.height;
      if(f.pos.x-f.radius>0) earliest=std::min(earliest,p.x-(f.pos.x-f.radius-1));
      if(f.pos.x+f.radius+1<f.width) earliest=std::min(earliest,f.pos.x+f.radius+1-p.x);
      if(f.pos.y-f.radius>0) earliest=std::min(earliest,p.y-(f.pos.y-f.radius-1));
      if(f.pos.y+f.radius+1<f.height) earliest=std::min(earliest,f.pos.y+f.radius+1-p.y);
      for(const auto &enemy:f.entities) if(enemy.type=="robot" && enemy.hp>0)
        earliest=std::min(earliest,distance(enemy.pos,p));
      return earliest;
    };
    std::vector<double> remaining(size,inf);std::vector<int> remaining_steps(size,0);
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> reverse_queue;
    remaining[destination]=0;reverse_queue.push({0,destination});
    while(!reverse_queue.empty()) {
      auto [cost,index]=reverse_queue.top();reverse_queue.pop();
      if(cost>remaining[index]+1e-6) continue;
      Pos q{index%f.width,index/f.width};
      for(auto step:delta) {
        Pos p=add(q,step);if(w.blocked(p,f.tick)) continue;
        int before=p.y*f.width+p.x;double next=cost+edge_cost(q);
        if(next+1e-6<remaining[before]
          || (std::abs(next-remaining[before])<1e-6 && remaining_steps[before]>remaining_steps[index]+1)) {
          remaining[before]=next;remaining_steps[before]=remaining_steps[index]+1;
          reverse_queue.push({next,before});
        }
      }
    }
    const Resource *pickup=nullptr;double best_pickup=0;
    const int baseline_first=preferred;
    for(const auto &[id,r]:w.resources) {
      (void)id;int i=r.pos.y*f.width+r.pos.x,arrival=steps[i];
      const double via_cost=costs[i]+remaining[i];
      const int via_steps=arrival+remaining_steps[i],extra_steps=via_steps-steps[destination];
      if(r.seen!=f.tick || r.pos==*goal || r.pos==f.pos || first[i]<0 || first[i]==baseline_first
        || !std::isfinite(costs[i]) || !std::isfinite(remaining[i]) || arrival<=0
        || extra_steps<0 || extra_steps>2 || via_cost>costs[destination]+extra_steps+1e-6) continue;
      // Cover every position from which a hidden rival could arrive no later
      // than this pickup. Manhattan lower bounds ignore walls conservatively.
      // This is a local race check, never knowledge of another car's intention.
      if(arrival>=earliest_rival(r.pos)) continue;
      if(extra_steps>0) {
        bool fresh_goal=std::any_of(w.resources.begin(),w.resources.end(),[&](const auto &other) {
          return other.second.pos==*goal && other.second.seen==f.tick;
        });
        // A detour must still beat this movement bound at the ORIGINAL goal;
        // equal-cost insertion needs no additional deadline allowance there.
        if(!fresh_goal || via_steps>=earliest_rival(*goal)) continue;
      }
      if(exposure(add(f.pos,delta[first[i]]))>exposure(add(f.pos,delta[baseline_first]))+1e-9) continue;
      double value=.25*std::min(r.value,std::max(0.0,120-f.resource_value))+task_reward(r.value,0,0,0);
      if(value<=0) continue;
      if(value>best_pickup+1e-9 || (pickup && std::abs(value-best_pickup)<1e-9
        && std::make_pair(arrival,r.pos)<std::make_pair(steps[pickup->pos.y*f.width+pickup->pos.x],pickup->pos))) {
        pickup=&r;best_pickup=value;
      }
    }
    if(pickup) {
      int i=pickup->pos.y*f.width+pickup->pos.x;preferred=first[i];
      resource_route_evaluation={{"target",{goal->x,goal->y}},{"pickup",{pickup->pos.x,pickup->pos.y}},
        {"pickup_value",pickup->value},{"pickup_points",best_pickup},{"pickup_arrival_steps",steps[i]},
        {"cost",costs[destination]},{"via_cost",costs[i]+remaining[i]},
        {"steps",steps[destination]},{"via_steps",steps[i]+remaining_steps[i]},
        {"pickup_rival_movement_lower_bound",earliest_rival(pickup->pos)},
        {"goal_rival_movement_lower_bound",earliest_rival(*goal)},
        {"baseline_first",dirs[baseline_first]},{"proposed_first",dirs[preferred]}};
    }
  }
  if(opening_information && !searched_first && goal && goal_type=="resource" && preferred>=0
    && std::any_of(w.resources.begin(),w.resources.end(),[&](const auto &r) {
      return r.second.pos==*goal && r.second.value<20;
    })) {
    auto exposure=[&](int d) {
      Pos next=add(f.pos,delta[d]);double result=0;
      for(const auto &enemy:f.entities) if(enemy.type=="robot" && !enemy.protected_spawn
        && aligned(next,enemy.pos)) result+=.6;
      return result;
    };
    auto novel=[&](int d) {
      Pos next=add(f.pos,delta[d]);int result=0;
      for(int x=std::max(0,next.x-f.radius);x<=std::min(f.width-1,next.x+f.radius);++x)
        for(int y=std::max(0,next.y-f.radius);y<=std::min(f.height-1,next.y+f.radius);++y)
          if(!w.explored.count({x,y})) ++result;
      return result;
    };
    int index=goal->y*f.width+goal->x,best=preferred,old_gain=novel(preferred),best_gain=old_gain;
    double old_exposure=exposure(preferred);
    // Isolate a blind first step rather than rerouting for every extra cell.
    // Earlier broad information bonuses did not reliably improve final score.
    for(int d=0;d<4 && old_gain==0;++d) if(equal_first[index] & (1<<d)) {
      int gain=novel(d);
      if(gain>best_gain && exposure(d)<=old_exposure+1e-9) {best=d;best_gain=gain;}
    }
    if(best!=preferred) {
      opening_route_evaluation={{"target",{goal->x,goal->y}},{"cost",costs[index]},{"steps",steps[index]},
        {"equal_first_mask",equal_first[index]},{"baseline_first",dirs[preferred]},{"proposed_first",dirs[best]},
        {"baseline_new_cells",old_gain},{"proposed_new_cells",best_gain}};
      preferred=best;
    }
  }
  const bool reverse=w.effect("illusion",f.team,f.tick), self_unrivaled=w.effect("unrivaled",f.team,f.tick);
  const bool purity=w.global("purity",f.tick);
  bool current_shot=false;
  if(config_.break_pursuit_cycles && !f.protected_spawn) for(const auto &enemy:f.entities)
    if(enemy.type=="robot" && !enemy.protected_spawn) {
      int range=static_cast<int>(w.effect_number("unrivaled",f.team,f.tick,"attack_radius",2));
      if((self_unrivaled && std::max(std::abs(f.pos.x-enemy.pos.x),std::abs(f.pos.y-enemy.pos.y))<=range)
        || (!self_unrivaled && normal_ray_clear(f.pos,enemy.pos,f.team,f.entities))) current_shot=true;
    }
  bool pursuit_pause=false;
  if(config_.break_pursuit_cycles && stage_==2 && f.tick>=max_ticks_/5 && f.tick+1<max_ticks_
    && f.effective_damage<500 && !f.protected_spawn && !current_shot && w.resources.empty()
    && (goal_type=="engage" || goal_type=="ambush")) {
    for(const auto &enemy:f.entities) if(enemy.type=="robot" && !enemy.protected_spawn && hp>=enemy.hp
      && std::abs(f.pos.x-enemy.pos.x)==1 && std::abs(f.pos.y-enemy.pos.y)==1
      && f.pos<enemy.pos) { // Geometry breaks simultaneous-wait symmetry, independent of seat labels.
      for(int period=2;period<=8 && !pursuit_pause;++period) {
        const Frame *before=w.at(f.tick-period);
        if(!before || before->pos!=f.pos) continue;
        bool complete=true;
        Pos next_self=f.pos,next_enemy=enemy.pos;
        for(int age=0;age<=period;++age) {
          const Frame *past=w.at(f.tick-age);
          if(!past || !past->alive || past->hp!=f.hp || past->resource_value!=f.resource_value
            || past->effective_damage!=f.effective_damage || past->completed!=f.completed) {complete=false;break;}
          auto rival=std::find_if(past->entities.begin(),past->entities.end(),[&](const auto &v) {
            return v.type=="robot" && v.team==enemy.team;
          });
          if(rival==past->entities.end() || rival->protected_spawn || rival->hp!=enemy.hp
            || (age>0 && (distance(past->pos,next_self)!=1 || distance(rival->pos,next_enemy)!=1))
            || (age==period && rival->pos!=enemy.pos)) {complete=false;break;}
          next_self=past->pos;next_enemy=rival->pos;
        }
        if(complete) pursuit_pause=true;
      }
    }
  }
  double reflection=20;
  for(const auto &e:w.events.items) if(e.kind=="purity" && e.active(f.tick)) reflection=e.number("reflection_damage",20);
  struct FireThreat {double attack,guarded,probability,probe_probability;};
  std::vector<FireThreat> threats;
  int uncertain_threats=0;double guarded_incoming=0;
  std::set<std::string> probe_directions,robust_directions;
  const Entity *probe_enemy=nullptr;
  double probe_incoming=0;
  double incoming=0;
  if(stage_==2 && !f.protected_spawn) for(const auto &v:f.entities) if(v.type=="robot" && !v.protected_spawn) {
    if(forecast && w.environmental_damage(v.pos,f.tick,v.protected_spawn)>=v.hp) continue;
    bool un=w.effect("unrivaled",v.team,f.tick);
    int radius=static_cast<int>(w.effect_number("unrivaled",v.team,f.tick,"attack_radius",2));
    bool ray=(forecast || config_.ray_occlusion)?normal_ray_clear(v.pos,f.pos,v.team,f.entities):aligned(v.pos,f.pos);
    if((un && std::max(std::abs(v.pos.x-f.pos.x),std::abs(v.pos.y-f.pos.y))<=radius) || (!un && ray)) {
      double probability=1;
      if(config_.standoff_fire_risk && config_.score_aware_combat && !purity
        && f.tick>=max_ticks_/5 && f.effective_damage<500 && uncertain_threats<3) {
        bool stable=true;
        for(int age=0;age<4;++age) {
          const Frame *past=w.at(f.tick-age);
          if(!past || !past->alive || past->pos!=f.pos || past->hp!=f.hp
            || past->resource_value!=f.resource_value || past->effective_damage!=f.effective_damage
            || past->completed!=f.completed) {stable=false;break;}
          auto enemy=std::find_if(past->entities.begin(),past->entities.end(),[&](const auto &e) {
            return e.type=="robot" && e.team==v.team;
          });
          if(enemy==past->entities.end() || !enemy->defending || enemy->protected_spawn
            || enemy->pos!=v.pos || enemy->hp!=v.hp) {stable=false;break;}
        }
        // A repeated past defense is evidence, not the next action. Keep a
        // substantial attack probability and value its lethal branch below.
        if(stable) {probability=.25;++uncertain_threats;}
      }
      double attack=un?w.effect_number("unrivaled",v.team,f.tick,"attack_damage",50):20;
      double guarded=un?w.effect_number("unrivaled",v.team,f.tick,"defended_damage",10):10;
      double probe_probability=probability;
      if((config_.standoff_attack_probe || config_.standoff_robust_trade) && config_.score_aware_combat && !purity
        && !self_unrivaled && !un && hp<=20 && v.hp<=20 && f.effective_damage<500
        && f.tick>=max_ticks_/3 && (f.tick+9<max_ticks_ || (config_.standoff_robust_trade && v.hp<=10))
        && std::count_if(f.entities.begin(),f.entities.end(),[](const auto &e){return e.type=="robot";})==1
        && v.hp>w.environmental_damage(v.pos,f.tick,false)
        && normal_ray_clear(f.pos,v.pos,f.team,f.entities)) {
        bool stable=true;
        for(int age=0;age<8;++age) {
          const Frame *past=w.at(f.tick-age);
          if(!past || !past->alive || past->protected_spawn || past->pos!=f.pos || past->hp!=f.hp
            || past->resource_value!=f.resource_value || past->effective_damage!=f.effective_damage
            || past->completed!=f.completed) {stable=false;break;}
          auto enemy=std::find_if(past->entities.begin(),past->entities.end(),[&](const auto &e) {
            return e.type=="robot" && e.team==v.team;
          });
          if(enemy==past->entities.end() || !enemy->defending || enemy->protected_spawn
            || enemy->pos!=v.pos || enemy->hp!=v.hp) {stable=false;break;}
        }
        const bool guaranteed_finisher=config_.standoff_robust_trade && v.hp<=10;
        if(stable || guaranteed_finisher) {
          // A heuristic, not knowledge of the next action. Only an attack
          // actually hitting this opponent may use this separate risk model.
          probe_probability=.25;
          probe_enemy=&v;
          for(int d=0;d<4;++d) {
            Pos offset{v.pos.x-f.pos.x,v.pos.y-f.pos.y};
            if(offset.x*delta[d].x+offset.y*delta[d].y>0) {
              if(stable && config_.standoff_attack_probe) probe_directions.insert(dirs[d]);
              if(config_.standoff_robust_trade) robust_directions.insert(dirs[d]);
            }
          }
        }
      }
      threats.push_back({attack,guarded,probability,probe_probability});
      incoming+=attack*probability;guarded_incoming+=guarded*probability;
      probe_incoming+=attack*probe_probability;
    }
  }
  // Multiple simultaneous threats retain the original conservative treatment.
  if(threats.size()!=1) {probe_directions.clear();robust_directions.clear();probe_enemy=nullptr;}
  using DamageOutcome=std::pair<double,double>; // Raw damage, probability.
  auto outcomes=[&](bool guarded,bool probe=false) {
    std::vector<DamageOutcome> result{{0,1}};
    for(const auto &threat:threats) {
      double damage=guarded?threat.guarded:threat.attack;
      double probability=probe?threat.probe_probability:threat.probability;
      if(probability==1) for(auto &outcome:result) outcome.first+=damage;
      else {
        auto before=result;result.clear();
        for(const auto &outcome:before) {
          result.push_back({outcome.first,outcome.second*(1-probability)});
          result.push_back({outcome.first+damage,outcome.second*probability});
        }
      }
    }
    return result;
  };
  std::vector<DamageOutcome> ordinary_risk,guarded_risk,probe_risk;
  if(uncertain_threats) {ordinary_risk=outcomes(false);guarded_risk=outcomes(true);}
  if(!probe_directions.empty()) probe_risk=outcomes(false,true);
  const double vulnerable=w.effect("pan",f.team,f.tick)?w.effect_number("pan",f.team,f.tick,"damage_multiplier",2):1;
  const Entity *purity_enemy=nullptr;
  if(config_.purity_joint_actions && config_.score_aware_combat && stage_==2 && purity
    && !forecast && !f.protected_spawn
    && std::count_if(f.entities.begin(),f.entities.end(),[](const auto &e){return e.type=="robot";})==1) {
    for(const auto &enemy:f.entities) if(enemy.type=="robot" && !enemy.protected_spawn
      && enemy.hp>w.environmental_damage(enemy.pos,f.tick,false)) {
      // Another car outside our view must not silently turn a full kill reward
      // into a shared reward. Allow the widest currently known area attack.
      int contributor_radius=2;
      for(const auto &event:w.events.items) if(event.kind=="unrivaled"
        && event.start<=f.tick && f.tick<event.end+event.number("effect_ticks",50))
        contributor_radius=std::max(contributor_radius,static_cast<int>(event.number("attack_radius",2)));
      for(const auto &effect:w.events.effects) if(effect.kind=="unrivaled" && effect.start<=f.tick && f.tick<effect.end)
        contributor_radius=std::max(contributor_radius,static_cast<int>(effect.params.value("attack_radius",2.0)));
      if(f.radius>=std::max(std::abs(enemy.pos.x-f.pos.x),std::abs(enemy.pos.y-f.pos.y))+contributor_radius)
        purity_enemy=&enemy;
    }
  }
  bool available_finisher=false;
  if((config_.productive_escape || config_.safe_progress_moves || resource_recovery) && !f.protected_spawn) for(const auto &enemy:f.entities)
    if(enemy.type=="robot" && !enemy.protected_spawn) {
      int radius=static_cast<int>(w.effect_number("unrivaled",f.team,f.tick,"attack_radius",2));
      bool reachable=self_unrivaled?std::max(std::abs(enemy.pos.x-f.pos.x),std::abs(enemy.pos.y-f.pos.y))<=radius
        :normal_ray_clear(f.pos,enemy.pos,f.team,f.entities);
      double damage=self_unrivaled?w.effect_number("unrivaled",f.team,f.tick,"attack_damage",50):20;
      if(w.effect("pan",enemy.team,f.tick))damage*=w.effect_number("pan",enemy.team,f.tick,"damage_multiplier",2);
      if(reachable && damage>=enemy.hp)available_finisher=true;
    }
  if(resource_recovery && (feedback.alternative || feedback.failures>=2)) {
    auto fresh=[&](Pos p) {
      return std::any_of(w.resources.begin(),w.resources.end(),[&](const auto &r) {
        return r.second.pos==p && r.second.seen==f.tick;
      });
    };
    const bool continuing=feedback.alternative.has_value();
    std::optional<Pos> blocked=continuing?feedback.blocked_entry:feedback.failed_entry;
    bool contention=blocked && fresh(*blocked) && !w.blocked(*blocked,f.tick) && !w.occupied(*blocked)
      && std::any_of(f.entities.begin(),f.entities.end(),[&](const auto &e) {
        return e.type=="robot" && e.hp>0 && distance(e.pos,*blocked)==1;
      });
    bool eligible=hp>incoming*vulnerable && !available_finisher && f.resource_value<120
      && (continuing || (contention && goal==blocked && goal_type=="resource"));
    std::vector<int> length(size,-1),route_first(size,-1),frontier(size);
    if(eligible) {
      // Only currently visible, empty, traversable cells form this bounded
      // alternative. The failed resource is still a resource, not a wall fact.
      int read=0,write=0;length[root]=0;frontier[write++]=root;
      while(read<write) {
        int index=frontier[read++];Pos p{index%f.width,index/f.width};
        if(length[index]>=2*f.radius) continue;
        for(int d=0;d<4;++d) {
          Pos q=add(p,delta[d]);
          if(!w.visible(q) || w.blocked(q,f.tick) || w.occupied(q) || (blocked && q==*blocked)) continue;
          int next=q.y*f.width+q.x;if(length[next]>=0) continue;
          length[next]=length[index]+1;route_first[next]=index==root?d:route_first[index];frontier[write++]=next;
        }
      }
    }
    std::optional<Pos> alternative;double rate=-1;int travel=0,rival_bound=0,direction=-1;
    for(const auto &[id,r]:w.resources) {
      (void)id;
      if(!eligible || r.seen!=f.tick || (blocked && r.pos==*blocked)
        || (continuing && r.pos!=*feedback.alternative)) continue;
      int index=r.pos.y*f.width+r.pos.x,n=length[index];
      if(n<=0 || f.tick+n>max_ticks_/5 || (continuing && f.tick+n>feedback.deadline)) continue;
      int rival=std::numeric_limits<int>::max();
      for(const auto &e:f.entities) if(e.type=="robot" && e.hp>0) rival=std::min(rival,distance(e.pos,r.pos));
      if(n>=rival) continue; // A visible lead only; hidden cars and future revivals remain unknown.
      int d=route_first[index];Pos next=add(f.pos,delta[d]);
      auto fire=[&](Pos p) {
        int count=0;
        for(const auto &e:f.entities) if(e.type=="robot" && !e.protected_spawn && aligned(e.pos,p)) ++count;
        return count;
      };
      if(fire(next)>fire(f.pos)) continue;
      double reward=std::min(r.value,120-f.resource_value)*.25+task_reward(r.value,0,0,0);
      double value=reward/std::pow(n+1,.85);
      if(value>rate) {rate=value;alternative=r.pos;travel=n;rival_bound=rival;direction=d;}
    }
    if(alternative) {
      if(!continuing) feedback.deadline=f.tick+travel+2;
      feedback.alternative=alternative;feedback.blocked_entry=blocked;
      resource_recovery_evaluation={{"status",continuing?"continued":"started"},
        {"blocked_entry",{blocked->x,blocked->y}},{"alternative",{alternative->x,alternative->y}},
        {"failed_receipts",feedback.failures},{"steps",travel},{"deadline",feedback.deadline},
        {"visible_rival_distance",rival_bound==std::numeric_limits<int>::max()?Json():Json(rival_bound)},
        {"hidden_rivals_known",false},{"proposed_first",dirs[direction]},{"value_per_cost",rate}};
      goal=alternative;goal_type="resource";best_goal=rate;preferred=direction;
      target_=goal;target_type_=goal_type;goal_reason=goal_type;
    } else {
      resource_recovery_evaluation={{"status",continuing?"cancelled":"no_alternative"},
        {"cause","availability_race_safety_or_deadline"},{"failed_receipts",feedback.failures}};
      feedback.alternative.reset();feedback.blocked_entry.reset();
    }
  }
  if(config_.safe_progress_moves && stage_==2 && f.tick>=max_ticks_/5 && f.tick+1<max_ticks_
    && f.radius>=4 && !f.protected_spawn && !available_finisher && !searched_first && goal && preferred>=0
    && (goal_type=="resource" || goal_type=="explore" || goal_type=="mission")
    && hp>incoming*vulnerable && hp-incoming*vulnerable<=40
    && !w.global("world_split",f.tick) && !w.global("world_split",f.tick+1)) {
    struct FiringPosition {Pos pos;int range;bool area;};
    std::vector<FiringPosition> firing;bool covered=true;
    for(const auto &enemy:f.entities) if(enemy.type=="robot" && enemy.hp>0) {
      // A currently protected robot may lose protection by the next Tick.
      // During a fortress event, allow a possible acquired area attack even
      // when the enemy's private residence progress is unknown.
      bool area=w.global("unrivaled",f.tick+1) || w.effect("unrivaled",enemy.team,f.tick+1);
      int range=area?static_cast<int>(w.effect_number("unrivaled",enemy.team,f.tick+1,"attack_radius",2)):2;
      if(f.radius<range+2) {covered=false;break;}
      firing.push_back({enemy.pos,range,area});
      for(auto direction:delta) {
        Pos p=add(enemy.pos,direction);
        // Unknown terrain is allowed in this possibility set, never stored
        // as a map fact. Ignore future occupancy and ray blockers to retain
        // a conservative geometric envelope, not an invented enemy action.
        if(!w.blocked(p,f.tick)) firing.push_back({p,range,area});
      }
    }
    auto exposed=[&](Pos p) {
      return std::any_of(firing.begin(),firing.end(),[&](const auto &shot) {
        if(p==shot.pos) return false;
        return shot.area?std::max(std::abs(p.x-shot.pos.x),std::abs(p.y-shot.pos.y))<=shot.range
          :((p.x==shot.pos.x || p.y==shot.pos.y) && distance(p,shot.pos)<=shot.range);
      });
    };
    Pos planned=add(f.pos,delta[preferred]);
    if(covered && exposed(planned) && !w.blocked(*goal,f.tick)) {
      std::vector<int> to_goal(size,-1),frontier(size);int read=0,write=0;
      int target=goal->y*f.width+goal->x;to_goal[target]=0;frontier[write++]=target;
      while(read<write) {
        int i=frontier[read++];Pos p{i%f.width,i/f.width};
        for(auto direction:delta) {
          Pos q=add(p,direction);if(w.blocked(q,f.tick)) continue;
          int next=q.y*f.width+q.x;if(to_goal[next]>=0) continue;
          to_goal[next]=to_goal[i]+1;frontier[write++]=next;
        }
      }
      int planned_distance=to_goal[planned.y*f.width+planned.x];
      int best=preferred,best_visits=std::numeric_limits<int>::max();
      double planned_event=w.environmental_damage(planned,f.tick+1,false);
      if(planned_distance>=0) for(int d=0;d<4;++d) {
        Pos p=add(f.pos,delta[d]);if(w.blocked(p,f.tick) || w.occupied(p) || exposed(p)) continue;
        int remaining=to_goal[p.y*f.width+p.x];
        if(remaining<0 || remaining>planned_distance || to_goal[root]!=remaining+1
          || remaining+1>max_ticks_-f.tick || w.environmental_damage(p,f.tick+1,false)>planned_event) continue;
        auto visit=w.visits.find(p);int count=visit==w.visits.end()?0:visit->second;
        if(count<best_visits) {best=d;best_visits=count;}
      }
      preferred=best;
    }
  }
  std::vector<Action> actions{{"wait",""}};
  if(stage_==2) {
    if(!f.protected_spawn) for(const auto &d:dirs) actions.push_back({"attack",d});
    // Stage two normally removes DEFEND. Re-enable it only while the purity
    // event is active, where attacking a defender reflects damage.
    if(config_.purity_defense && w.global("purity",f.tick)) actions.push_back({"defend",""});
  }
  for(const auto &d:dirs) actions.push_back({"move",d});
  Action best; double best_score=-inf;
  for(const auto &a:actions) {
    const bool robust_trade=config_.standoff_robust_trade && !config_.standoff_fire_risk
      && a.type=="attack" && robust_directions.count(a.direction)
      && f.radius>=std::max(std::abs(f.pos.x-probe_enemy->pos.x),std::abs(f.pos.y-probe_enemy->pos.y))+2
      && !(goal && *goal==f.pos && (goal_type=="resource" || goal_type=="mission"));
    const bool attack_probe=!robust_trade && config_.standoff_attack_probe
      && a.type=="attack" && probe_directions.count(a.direction);
    const bool probabilistic=uncertain_threats || attack_probe;
    double score=a.type=="wait"?-.3:-.15, received=(attack_probe?probe_incoming:incoming)*vulnerable;
    if(a.type=="defend" && config_.purity_defense && purity) score+=std::min(1.5,incoming*.03);
    if(pursuit_pause && a.type=="wait") score+=1.2;
    double predicted_attack=0,predicted_reward=0,tempo_bonus=0;
    double movement_utility=0,residence_credit=0;
    struct JointOutcome {double probability,received,attack,reward;};
    std::vector<JointOutcome> purity_risk;
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
      if(escape_damage.empty()) score-=event_cost(end,f.tick+1,false);
      if(stage_==2) for(const auto &e:w.events.items) if(e.active(f.tick)) {
        if(e.kind=="angel" && e.contains(end) && (!config_.event_timing || !w.effect("angel",f.team,f.tick)))
          score+=std::min(100-f.hp,e.number("heal_per_tick",10)*e.number("effect_ticks",10))*.025;
        if(e.kind=="pepper") for(auto point:e.points) if(point!=end && (point.x==end.x || point.y==end.y))
          for(const auto &v:f.entities) if(v.type=="robot" && distance(v.pos,point)==1) score-=e.number("damage",50)*.08;
      }
      // All of these route/entry/visit effects require surviving combat and
      // executing the move. Retain only the ordinary action cost if cancelled.
      movement_utility=score+.15;
    } else if(a.type=="defend") {
      received=0;
      if(config_.standoff_fire_risk) received=purity?0:guarded_incoming*vulnerable;
      else if(!purity && !f.protected_spawn) for(const auto &v:f.entities) if(v.type=="robot" && !v.protected_spawn) {
        if(forecast && w.environmental_damage(v.pos,f.tick,v.protected_spawn)>=v.hp) continue;
        bool un=w.effect("unrivaled",v.team,f.tick);
        int radius=static_cast<int>(w.effect_number("unrivaled",v.team,f.tick,"attack_radius",2));
        bool ray=(forecast || config_.ray_occlusion)?normal_ray_clear(v.pos,f.pos,v.team,f.entities):aligned(v.pos,f.pos);
        if((un && std::max(std::abs(v.pos.x-f.pos.x),std::abs(v.pos.y-f.pos.y))<=radius) || (!un && ray))
          received+=(un?w.effect_number("unrivaled",v.team,f.tick,"defended_damage",10):10)*vulnerable;
      }
    } else if(a.type=="attack") {
      std::vector<const Entity*> targets; int dir=std::find(dirs.begin(),dirs.end(),a.direction)-dirs.begin();
      for(const auto &v:f.entities) if(v.type=="robot") {
        if((forecast || config_.score_aware_combat) && w.environmental_damage(v.pos,f.tick,v.protected_spawn)>=v.hp) continue;
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
        double available_hp=(forecast || config_.score_aware_combat)?std::max(0.0,v->hp-w.environmental_damage(v->pos,f.tick,v->protected_spawn)):v->hp;
        dealt[v->team]=std::min(available_hp,damage);
        int discount_until=config_.damage_scale_ticks<0?max_ticks_/5:config_.damage_scale_ticks;
        // Finite resources can be lost permanently; ordinary damage can
        // usually be earned later. Preserve the existing score for a possible
        // immediate finisher, even when a defended shot may not finish it.
        double scale=(f.tick<discount_until && base*mult<available_hp)?config_.early_damage_scale:1;
        if(!config_.score_aware_combat) {
          score+=std::min(available_hp,damage)*.09*scale;
          if(damage>=available_hp) score+=.9;
        } else {
          // Only current visible potential contributors affect the estimate.
          // They may choose other actions; their private plans are unknown.
          int contributors=1;
          double other_damage=0;
          auto blockers=f.entities;
          blockers.push_back({"robot",f.team,f.team,f.pos,f.hp,0,false,f.protected_spawn});
          for(const auto &other:f.entities) if(other.type=="robot" && other.team!=v->team
            && !other.protected_spawn && other.hp>w.environmental_damage(other.pos,f.tick,false)) {
            bool un=w.effect("unrivaled",other.team,f.tick);
            int range=static_cast<int>(w.effect_number("unrivaled",other.team,f.tick,"attack_radius",2));
            if((un && std::max(std::abs(other.pos.x-v->pos.x),std::abs(other.pos.y-v->pos.y))<=range)
              || (!un && normal_ray_clear(other.pos,v->pos,other.team,blockers))) {
              ++contributors;
              double other_base=un?w.effect_number("unrivaled",other.team,f.tick,"attack_damage",50):20;
              double other_guard=un?w.effect_number("unrivaled",other.team,f.tick,"defended_damage",10):10;
              other_damage+=((1-defend_probability)*other_base+(purity?0:defend_probability*other_guard))*mult;
            }
          }
          // If the visible potential contributors all shoot, overkill is
          // apportioned by nominal damage. This is a conservative estimate.
          predicted_attack+=std::min(damage,available_hp*damage/std::max(1e-9,damage+other_damage))*scale;
          double finish=(base*mult>=available_hp?1-defend_probability:0)
            +(!purity && defended*mult>=available_hp?defend_probability:0);
          predicted_reward+=60*finish/contributors;
          // Removing a visible opponent also stops its movement/collection and
          // costs respawn time. This is a small tempo value, not private task data.
          score+=.5*finish;tempo_bonus+=.5*finish;
        }
        if(purity && v==purity_enemy) {
          // The past defending flag only supplies the existing heuristic prior.
          // Conservatively fold non-defense actions into a possible shot at us;
          // the same car cannot both shoot and reflect our attack this Tick.
          double shot=std::min(available_hp,base*mult);
          purity_risk={{1-defend_probability,incoming*vulnerable,shot*scale,shot>=available_hp?60.0:0.0},
                       {defend_probability,reflection,0,0}};
          received=(1-defend_probability)*incoming*vulnerable+defend_probability*reflection;
          predicted_attack=(1-defend_probability)*shot*scale;
          predicted_reward=(1-defend_probability)*(shot>=available_hp?60:0);
          dealt[v->team]=(1-defend_probability)*shot;
        } else if(purity) received+=defend_probability*reflection;
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
        if(e.end-f.tick>=std::max(1.0,e.number("stay_ticks",10)-residence)) {score+=.9;residence_credit+=.9;}
      }
    const double before_combat_risk=score;
    score-=received*.045;
    double death_probability=received>=hp?1:0;
    const auto &risk=attack_probe?probe_risk:(a.type=="defend"?guarded_risk:ordinary_risk);
    double raw_expected=attack_probe?probe_incoming:(a.type=="defend"?(purity?0:guarded_incoming):incoming);
    double fixed_received=std::max(0.0,received-raw_expected*vulnerable);
    if(probabilistic) {
      death_probability=0;
      for(const auto &[damage,probability]:risk)
        if((a.type=="defend" && purity?0:damage*vulnerable)+fixed_received>=hp)
          death_probability+=probability;
    }
    if(!purity_risk.empty()) {
      death_probability=0;
      for(const auto &branch:purity_risk) if(branch.received>=hp) death_probability+=branch.probability;
    }
    score-=3*death_probability;
    const double progress_survival=(config_.survival_conditioned_progress && stage_==2)
      ?std::clamp(1-death_probability,0.0,1.0):1;
    if(config_.survival_conditioned_progress && stage_==2)
      score-=death_probability*(movement_utility+residence_credit);
    if(config_.score_aware_combat && stage_==2) {
      double after=projected_damage(f.effective_damage,predicted_attack,predicted_reward,received>=hp?1:0);
      score+=1.8*(combat_score(after)-combat_score(f.effective_damage));
      bool finite_done=w.resources.empty() && w.explored.size()>=static_cast<size_t>(size*.95)
        && std::all_of(f.missions.begin(),f.missions.end(),[&](const auto &m) {
          return f.completed.count(m.at("id").template get<std::string>());
        });
      // Excess adjusted damage is still the fourth tie-break. Its local value
      // rises only when known resources, exploration and public tasks are done.
      double tie_weight=finite_done?.054:.006;
      double reserve=(f.tick>=max_ticks_/5 && max_ticks_-f.tick>4)?config_.damage_reserve:0;
      double excess_before=std::max(0.0,f.effective_damage-500),excess_after=std::max(0.0,after-500);
      // A full score is fragile when one death can take it below 500. Value a
      // small reserve after the finite-resource phase; it is not extra points.
      score+=.054*(std::min(reserve,excess_after)-std::min(reserve,excess_before));
      score+=tie_weight*(std::max(0.0,excess_after-reserve)-std::max(0.0,excess_before-reserve));
      auto value=[&](double damage) {
        double excess=std::max(0.0,damage-500);
        return 1.8*combat_score(damage)+.054*std::min(reserve,excess)
          +tie_weight*std::max(0.0,excess-reserve);
      };
      if(!purity_risk.empty()) {
        double expected=0;Json branches=Json::array();
        for(const auto &branch:purity_risk) {
          double result=projected_damage(f.effective_damage,branch.attack,branch.reward,branch.received>=hp?1:0);
          expected+=branch.probability*value(result);
          branches.push_back({{"probability",branch.probability},{"received",branch.received},
            {"attack",branch.attack},{"kill_reward",branch.reward},{"dies",branch.received>=hp},
            {"adjusted_damage",result}});
        }
        score+=expected-value(after);
        purity_evaluations.push_back({{"direction",a.direction},{"enemy",purity_enemy->team},
          {"hp_after_environment",hp},{"expected_received",received},{"death_probability",death_probability},
          {"branches_nondefend_then_defend",branches},{"score",score}});
      }
      if(probabilistic) {
        double expected=0;
        if(attack_probe) {
          // Couple the one visible opponent's action with both its received
          // damage and its return fire. This retains cap/floor/death effects;
          // it does not pretend the heuristic action frequencies are known.
          double available=std::max(0.0,probe_enemy->hp-w.environmental_damage(probe_enemy->pos,f.tick,false));
          double mult=w.effect("pan",probe_enemy->team,f.tick)
            ?w.effect_number("pan",probe_enemy->team,f.tick,"damage_multiplier",2):1;
          auto branch=[&](double shot,double received) {
            double contribution=std::min(available,shot*mult);
            return value(projected_damage(f.effective_damage,contribution,contribution>=available?60:0,
              received+fixed_received>=hp?1:0));
          };
          expected=.25*branch(20,20*vulnerable)+.65*branch(10,0)+.10*branch(20,0);
        } else for(const auto &[damage,probability]:risk) {
          double loss=(a.type=="defend" && purity?0:damage*vulnerable)+fixed_received;
          expected+=probability*value(projected_damage(f.effective_damage,predicted_attack,predicted_reward,loss>=hp?1:0));
        }
        // Preserve the existing valuation outside this observed standoff. In
        // the experiment, include death loss even if mean incoming is sublethal.
        score+=expected-value(after);
      }
      if(robust_trade) {
        double available=std::max(0.0,probe_enemy->hp-w.environmental_damage(probe_enemy->pos,f.tick,false));
        double mult=w.effect("pan",probe_enemy->team,f.tick)
          ?w.effect_number("pan",probe_enemy->team,f.tick,"damage_multiplier",2):1;
        double worst=inf;bool gains_in_every_branch=true;
        auto branch=[&](double shot,double incoming) {
          double contribution=std::min(available,shot*mult);
          bool finish=contribution>=available,death=incoming>=hp;
          double result=projected_damage(f.effective_damage,contribution,finish?60:0,death?1:0);
          if(combat_score(result)<=combat_score(f.effective_damage)+1e-9) gains_in_every_branch=false;
          worst=std::min(worst,value(result)-value(f.effective_damage)
            -.045*std::min(hp,incoming)+(finish?.5:0)
            -(config_.survival_conditioned_progress && death?residence_credit:0));
        };
        branch(20,20*vulnerable); // They fire: simultaneous damage, rewards, then deaths.
        branch(10,0);             // They defend: reduced outgoing damage, no return shot.
        branch(20,0);             // They wait/move: combat occurs before movement.
        // In an already observed fixed point, require immediate points even
        // in the worst legal branch, rather than charging the same fixed
        // death aversion on top of the actual death deduction. Respawn still
        // loses future opportunities; the complete-match tests must assess it.
        if(gains_in_every_branch) score=before_combat_risk-tempo_bonus+worst;
      }
    }
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
    if(!escape_damage.empty()) {
      // Preserve a route out of lethal future damage. Survivable damage keeps
      // the existing one-step valuation, so advance notice does not by itself
      // sacrifice finite resources just to preserve HP. Current combat still
      // resolves before movement; moving cannot evade that received damage.
      const bool protected_next=f.protected_spawn && a.type!="move";
      if(!protected_next && received<hp && escape_damage[end.y*f.width+end.x]>=hp-received)
        score-=progress_survival*100;
      else score-=progress_survival*event_cost(end,f.tick+1,protected_next);
    } else if(a.type!="move") score-=progress_survival*event_cost(end,f.tick+1,f.protected_spawn);
    // Collection and coordinate tasks are checked after movement. When the
    // current cell is the goal, staying alive here completes it; moving away
    // would forfeit the objective even though the route has already arrived.
    if(stage_==2 && goal && *goal==f.pos && (goal_type=="resource" || goal_type=="mission") && a.type!="move")
      score+=progress_survival*std::min(4.0,best_goal);
    if(stage_==2 && config_.event_timing && goal && *goal==f.pos && goal_type=="heal_stage" && a.type!="move")
      score+=progress_survival*std::min(4.0,best_goal);
    if(stage_==1 && goal && *goal==f.pos && a.type=="wait") score=2;
    if(score>best_score) {best_score=score;best=a;}
  }
  reason=goal_type;
  if(best.type=="attack" || best.type=="defend") reason="combat";
  if(!legal(best,f,stage_)) return {};
  if(resource_recovery) {
    feedback.proposed_tick=f.tick;feedback.from=f.pos;feedback.action=best;feedback.proposed_entry.reset();
    if(best.type=="move" && goal && goal_type=="resource" && distance(f.pos,*goal)==1) {
      int d=std::find(dirs.begin(),dirs.end(),best.direction)-dirs.begin();
      if(add(f.pos,delta[d])==*goal) feedback.proposed_entry=goal;
    }
  }
  return best;
}
} // namespace rmtc
