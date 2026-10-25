#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <deque>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace rmtc {
using Json = nlohmann::json;
// Fixed diagnostic labels only; never returns arbitrary exception text.
std::string model_schema_error_code(const std::exception &error);
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
// Public v1.1 score formula. Perception remains authoritative; this is only
// used to value hypothetical actions, never to reconstruct received score.
double combat_score(double adjusted_damage);
double projected_damage(double previous, double attack, double reward, int deaths);
struct Event {
  std::string kind;
  int start{}, end{};
  std::optional<std::array<Pos,2>> rect;
  std::vector<Pos> points;
  Json params = Json::object();
  std::string id; // Local opaque identity exposed only in the revision experiment.
  int revision{};
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
  // A versioned request snapshot permits explicit model revisions. A missing
  // snapshot retains legacy merge semantics; same-kind/Tick is never identity.
  Json merge(const Json &j, const Frame &f, const Json *request_context=nullptr);
  Json context(bool versioned=false) const;
  void clear();
  std::vector<Event> items;
  std::vector<Effect> effects;
private:
  int next_id_{};
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
  std::map<std::string,int> recent_competition;
  std::map<Pos,int> visits;
  int normal_collected{}, high_collected{}; // Confirmed lower bounds from own perception.
  std::optional<Pos> spawn;
};
struct PlannerConfig {
  bool stage1_subgoal_risk{false}; // Legal-view contingent subgoal evaluation.
  bool stage1_joint_coverage{false}; // Experimental joint resource/visibility tour.
  bool stage1_resource_route{false}; // Single-car budgeted route; stage two is unchanged.
  double early_movement_scale{1.0};
  double early_damage_scale{1.0};
  int damage_scale_ticks{-1}; // -1: first fifth of this match.
  bool resource_paths_exact{false};
  int resource_lookahead{3};
  bool tactical_forecast{false};
  int forecast_start_tick{0}; // -1: after the first fifth; 0 preserves the full-match experiment.
  bool early_interception{false};
  bool frontier_lookahead{false};
  bool information_paths{false};
  bool race_aware_resources{false};
  bool contested_task_resources{false}; // Experimental: locally winnable public-task resource races.
  bool competitive_exploration{false}; // Experimental: softly discount unknown cells a visible rival can reach earlier.
  bool finish_early_exploration{false}; // Experimental: retain a reachable early exploration plan before optional hunting.
  bool opening_route_information{false}; // Experimental: more first-step vision on equal-cost normal-resource routes.
  bool survival_conditioned_progress{false}; // Experimental: value post-combat progress only in predicted survival branches.
  bool respawn_task_route{false}; // Experimental: use an observed revival to start a higher-rate public task route.
  bool safe_progress_moves{false}; // Experimental: equally short finite-goal steps outside next-Tick fire envelopes.
  bool break_pursuit_cycles{true}; // One-step pause in a repeatedly observed pursuit orbit, with HP/score guards.
  bool coordinate_before_hunt{false}; // Experimental: finish a reachable late public coordinate task before optional pursuit.
  bool standoff_fire_risk{false}; // Experimental: uncertain fire after a continuous observed defense standoff.
  bool standoff_attack_probe{false}; // Experimental: attack-only risk after a prolonged low-HP single-enemy standoff.
  bool standoff_robust_trade{true}; // Worst joint-action score gain in an observed low-HP standoff or guaranteed finisher.
  bool purity_joint_actions{false}; // Experimental: joint attack/defend outcomes for one fully observed opponent.
  bool resource_beam{false};
  bool transient_occupancy{true};
  bool event_timing{false}; // Experimental event entry/wait/residence planning.
  bool fortress_time_budget{false}; // Isolate fortress waiting/residence cost from angel timing.
  bool productive_escape{true}; // Early collection/exploration progress out of current fire.
  bool validated_enemy_memory{false}; // Experimental: stop targeting disproved old positions.
  bool event_escape{false}; // Experimental: preserve a route out before known event activation.
  bool use_event_interpretation{true}; // Ablation only: AI calls/validation continue when false.
  bool event_unavoidable_cost{false}; // Experimental: remove fixed future damage from route comparison.
  bool frontier_distance_window{false}; // Experimental: compare exploration over a vision-sized travel window.
  bool ray_occlusion{false}; // Experimental: visible robots block incoming normal shots.
  bool score_aware_combat{true}; // v1.1 marginal points, kill reward and death loss.
  int damage_reserve{120}; // With score-aware combat, a two-death buffer after the resource phase.
};
class Planner {
public:
  explicit Planner(int stage=2, int max_ticks=500, PlannerConfig config={});
  Action decide(const World &w);
  void reset() { stage1_subgoal_session_.clear(); stage1_subgoal_goal_.reset(); stage1_subgoal_evaluation_=Json(); stage1_joint_session_.clear(); stage1_joint_route_.clear(); stage1_joint_resources_.clear(); stage1_joint_walls_.clear(); stage1_joint_evaluation_=Json(); stage1_cache_session_.clear(); stage1_certified_incumbent_.clear(); stage1_cache_points_.clear(); stage1_cache_walls_.clear(); stage1_cache_distances_.clear(); stage1_route_evaluation=Json(); target_.reset(); target_type_.clear(); recovery_mission_.reset(); goal_reason.clear(); purity_evaluations=Json::array(); opening_route_evaluation=Json(); reason = "reset"; }
  std::string reason;
  std::string goal_reason; // Selected goal before an immediate combat action overrides reason.
  Json purity_evaluations=Json::array(); // Local estimates only; never fed back as observations.
  Json stage1_route_evaluation; // Predictions from own observations only.
  Json opening_route_evaluation; // A hypothetical equal-cost route choice, not a resource observation.
  std::optional<Pos> target() const {return target_;}
private:
  std::optional<Action> decide_stage1_subgoal(const World &w);
  std::string stage1_subgoal_session_;
  uint64_t stage1_subgoal_knowledge_{};
  uint32_t stage1_subgoal_seed_{};
  std::optional<Pos> stage1_subgoal_goal_;
  Json stage1_subgoal_evaluation_;
  std::optional<Action> decide_stage1_joint(const World &w);
  std::string stage1_joint_session_;
  std::vector<Pos> stage1_joint_route_,stage1_joint_resources_,stage1_joint_walls_;
  Json stage1_joint_evaluation_;

  std::optional<Action> decide_stage1(const World &w);
  std::string stage1_cache_session_;
  int stage1_cache_width_{}, stage1_cache_height_{};
  std::vector<Pos> stage1_cache_points_, stage1_cache_walls_;
  std::vector<Pos> stage1_certified_incumbent_;
  std::vector<std::vector<int>> stage1_cache_distances_;
  int stage_, max_ticks_;
  PlannerConfig config_;
  std::optional<Pos> target_;
  std::optional<std::string> recovery_mission_;
  std::string target_type_;
};
} // namespace rmtc
