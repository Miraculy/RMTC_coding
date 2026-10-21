#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace rmtc {
using Json = nlohmann::json;
struct Pos {
  int x{}, y{};
  bool operator==(Pos b) const { return x == b.x && y == b.y; }
  bool operator!=(Pos b) const { return !(*this == b); }
  bool operator<(Pos b) const { return std::tie(x,y) < std::tie(b.x,b.y); }
};
int distance(Pos a, Pos b);
Pos relative_position(const std::string &text);
struct Entity {
  std::string type, id, team;
  Pos pos;
  double hp{100}, value{};
  bool defending{}, protected_spawn{};
};
struct Frame {
  int tick{}, width{}, height{}, radius{};
  std::string session, team, event_text;
  Pos pos;
  double hp{}, resource_value{}, effective_damage{};
  bool alive{}, protected_spawn{}, moved{};
  int action_tick{-1};
  Json missions = Json::array();
  std::set<std::string> completed;
  std::vector<Entity> entities;
};
Frame parse_frame(const Json &j);
struct Action {
  std::string type{"wait"}, direction;
  Json json(const Frame &f) const;
};
bool legal(const Action &a, const Frame &f, int stage);
struct Event {
  std::string kind;
  int start{}, end{};
  std::optional<std::array<Pos,2>> rect;
  std::vector<Pos> points;
  Json params = Json::object();
  bool active(int t) const { return start <= t && t < end; }
  bool contains(Pos p) const;
  double number(const std::string &key, double fallback) const;
};
struct Effect {
  std::string kind, team;
  int start{}, end{};
  Json params = Json::object();
};
class Events {
public:
  // Only accepts model-produced structured data, never interprets announcement text.
  void merge(const Json &j, const Frame &f);
  Json context() const;
  void clear();
  std::vector<Event> items;
  std::vector<Effect> effects;
};
struct Resource { Pos pos; double value; int seen; };
struct Obstacle { int first, last; };
class World {
public:
  // Returns false for duplicate/old frames. A new session resets all match memory.
  bool update(const Frame &f);
  bool inside(Pos p) const;
  bool visible(Pos p) const;
  bool blocked(Pos p, int tick) const;
  bool occupied(Pos p) const;
  bool effect(const std::string &kind, const std::string &team, int tick) const;
  double effect_number(const std::string &kind, const std::string &team,
                       int tick, const std::string &key, double fallback) const;
  bool global(const std::string &kind, int tick) const;
  double environmental_damage(Pos p, int tick, bool protected_spawn) const;
  bool split_line(Pos p, const Event &e) const;
  const Frame *at(int tick) const;
  Frame current;
  Events events;
  std::deque<Frame> history;
  std::set<Pos> explored;
  std::map<Pos, Obstacle> obstacles;
  std::map<std::string, Resource> resources;
  std::map<std::string, std::pair<Entity,int>> enemies;
  std::map<Pos,int> visits;
  std::optional<Pos> spawn;
};
class Planner {
public:
  explicit Planner(int stage=2, int max_ticks=500): stage_(stage), max_ticks_(max_ticks) {}
  Action decide(const World &w);
  void reset() { target_.reset(); reason = "reset"; }
  std::string reason;
private:
  int stage_, max_ticks_;
  std::optional<Pos> target_;
};
} // namespace rmtc
