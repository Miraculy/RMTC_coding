#include "rmtc_bot/ai.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

namespace rmtc {
std::string env(const char *key,const std::string &fallback="") {
  const char *value=std::getenv(key);return value?value:fallback;
}
class Contestant final:public rclcpp::Node {
public:
  Contestant():Node("rmtc_contestant") {
    team_=declare_parameter<std::string>("team","A");
    name_=declare_parameter<std::string>("team_name","图灵探索者");
    stage_=declare_parameter<int>("stage",2);
    max_ticks_=declare_parameter<int>("max_ticks",stage_==1?100:500);
    if(team_.size()!=1 || std::string("ABCD").find(team_)==std::string::npos || (stage_!=1 && stage_!=2) || max_ticks_<=0)
      throw std::invalid_argument("invalid team/stage/max_ticks");
    if(name_.empty() || name_.size()>96 || name_.find_first_of("\r\n")!=std::string::npos)
      throw std::invalid_argument("invalid team_name");
    PlannerConfig planner_config;
    planner_config.early_movement_scale=declare_parameter<double>("early_movement_scale",1.0);
    planner_config.early_damage_scale=declare_parameter<double>("early_damage_scale",1.0);
    planner_config.damage_scale_ticks=declare_parameter<int>("damage_scale_ticks",-1);
    planner_config.resource_paths_exact=declare_parameter<bool>("resource_paths_exact",false);
    planner_config.resource_lookahead=declare_parameter<int>("resource_lookahead",3);
    planner_config.tactical_forecast=declare_parameter<bool>("tactical_forecast",false);
    planner_config.forecast_start_tick=declare_parameter<int>("forecast_start_tick",0);
    planner_config.early_interception=declare_parameter<bool>("early_interception",false);
    planner_config.frontier_lookahead=declare_parameter<bool>("frontier_lookahead",false);
    planner_config.information_paths=declare_parameter<bool>("information_paths",false);
    planner_config.race_aware_resources=declare_parameter<bool>("race_aware_resources",false);
    planner_config.contested_task_resources=declare_parameter<bool>("contested_task_resources",false);
    planner_config.competitive_exploration=declare_parameter<bool>("competitive_exploration",false);
    planner_config.finish_early_exploration=declare_parameter<bool>("finish_early_exploration",false);
    planner_config.opening_route_information=declare_parameter<bool>("opening_route_information",false);
    planner_config.resource_route_pickups=declare_parameter<bool>("resource_route_pickups",false);
    planner_config.resource_race_yield=declare_parameter<bool>("resource_race_yield",false);
    planner_config.resource_fresh_returns=declare_parameter<bool>("resource_fresh_returns",false);
    planner_config.resource_blocked_recovery=declare_parameter<bool>("resource_blocked_recovery",false);
    planner_config.observation_detour=declare_parameter<bool>("observation_detour",false);
    planner_config.observation_extra_limit=declare_parameter<int>("observation_extra_limit",2);
    planner_config.observation_attempt_limit=declare_parameter<int>("observation_attempt_limit",99);
    planner_config.observation_min_tick=declare_parameter<int>("observation_min_tick",0);
    planner_config.observation_max_known_resources=declare_parameter<int>("observation_max_known_resources",99);
    planner_config.resource_arrival_value=declare_parameter<bool>("resource_arrival_value",false);
    planner_config.survival_conditioned_progress=declare_parameter<bool>("survival_conditioned_progress",false);
    planner_config.respawn_task_route=declare_parameter<bool>("respawn_task_route",false);
    planner_config.safe_progress_moves=declare_parameter<bool>("safe_progress_moves",false);
    planner_config.break_pursuit_cycles=declare_parameter<bool>("break_pursuit_cycles",true);
    planner_config.coordinate_before_hunt=declare_parameter<bool>("coordinate_before_hunt",false);
    planner_config.standoff_fire_risk=declare_parameter<bool>("standoff_fire_risk",false);
    planner_config.standoff_attack_probe=declare_parameter<bool>("standoff_attack_probe",false);
    planner_config.standoff_robust_trade=declare_parameter<bool>("standoff_robust_trade",true);
    planner_config.purity_joint_actions=declare_parameter<bool>("purity_joint_actions",false);
    planner_config.resource_beam=declare_parameter<bool>("resource_beam",false);
    planner_config.transient_occupancy=declare_parameter<bool>("transient_occupancy",true);
    planner_config.event_timing=declare_parameter<bool>("event_timing",false);
    planner_config.fortress_time_budget=declare_parameter<bool>("fortress_time_budget",false);
    planner_config.productive_escape=declare_parameter<bool>("productive_escape",true);
    planner_config.validated_enemy_memory=declare_parameter<bool>("validated_enemy_memory",false);
    planner_config.event_escape=declare_parameter<bool>("event_escape",false);
    use_event_interpretation_=declare_parameter<bool>("use_event_interpretation",true);
    planner_config.use_event_interpretation=use_event_interpretation_;
    planner_config.event_unavoidable_cost=declare_parameter<bool>("event_unavoidable_cost",false);
    planner_config.frontier_distance_window=declare_parameter<bool>("frontier_distance_window",false);
    planner_config.ray_occlusion=declare_parameter<bool>("ray_occlusion",false);
    planner_config.score_aware_combat=declare_parameter<bool>("score_aware_combat",true);
    planner_config.damage_reserve=declare_parameter<int>("damage_reserve",120);
    planner_=std::make_unique<Planner>(stage_,max_ticks_,planner_config);
    token_file_=declare_parameter<std::string>("token_file","");
    AiConfig config;
    config.provider=declare_parameter<std::string>("ai_provider",env("RMTC_AI_PROVIDER","gateway"));
    deepseek_=config.provider=="deepseek";
    config.url=declare_parameter<std::string>("ai_gateway",env("AI_GATEWAY",deepseek_?"https://api.deepseek.com/chat/completions":"http://127.0.0.1:8080/v1/chat/completions"));
    config.model=declare_parameter<std::string>("ai_model",env("RMTC_AI_MODEL",deepseek_?"deepseek-flash":""));
    config.timeout_ms=declare_parameter<int>("ai_timeout_ms",12000);
    config.budget=declare_parameter<int>("ai_budget",90);
    config.max_tokens=declare_parameter<int>("ai_max_tokens",2500);
    event_revisions_=declare_parameter<bool>("event_revisions",false);
    config.event_revisions=event_revisions_;
    ai_enabled_=declare_parameter<bool>("ai_enabled",true) && stage_==2;
    ai_action_revision_=declare_parameter<bool>("ai_action_revision",false);
    revision_window_ms_=declare_parameter<int>("revision_window_ms",350);
    if(revision_window_ms_<=0 || revision_window_ms_>5000)
      throw std::invalid_argument("invalid action revision window");
    if(ai_enabled_)ai_=std::make_unique<AiClient>(config);
    auto log_dir=declare_parameter<std::string>("log_dir",env("RMTC_LOG_DIR",env("HOME","/tmp")+"/.rmtc-bot"));
    std::filesystem::create_directories(log_dir);
    auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    log_.open(log_dir+"/"+team_+"-"+std::to_string(stamp)+".jsonl");
    if(!log_)throw std::runtime_error("cannot open contestant log");
    auto realtime=rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile();
    auto retained=rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
    action_pub_=create_publisher<std_msgs::msg::String>("/team_"+team_+"/action",realtime);
    registration_=create_publisher<std_msgs::msg::String>("/team_"+team_+"/registration",retained);
    perception_=create_subscription<std_msgs::msg::String>("/team_"+team_+"/perception",realtime,
      [this](std_msgs::msg::String::ConstSharedPtr msg){perception(msg->data);});
    mission_=create_subscription<std_msgs::msg::String>("/referee/mission",retained,
      [this](std_msgs::msg::String::ConstSharedPtr msg){mission_text_=msg->data.substr(0,32768);});
    notices_=create_subscription<std_msgs::msg::String>("/referee/event_text",rclcpp::QoS(128).reliable().transient_local(),
      [this](std_msgs::msg::String::ConstSharedPtr msg){
        if(ai_ && !world_.history.empty() && msg->data.size()<=131072)
          ai_->submit(world_.current,world_.events,msg->data,"topic");
      });
    registry_=create_subscription<std_msgs::msg::String>("/referee/team_registry",retained,
      [](std_msgs::msg::String::ConstSharedPtr){});
    register_timer_=create_wall_timer(std::chrono::seconds(2),[this]{register_name();});
    ai_timer_=create_wall_timer(std::chrono::milliseconds(20),[this]{collect_ai();});
    register_name();
    RCLCPP_INFO(get_logger(),"C++ contestant ready: team=%s stage=%d AI=%s",team_.c_str(),stage_,ai_enabled_?"async":"disabled");
  }
private:
  void record(Json item) {
    item["wall_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    log_<<item.dump()<<'\n';log_.flush();
    if(!log_ && !log_failed_) {log_failed_=true;RCLCPP_ERROR(get_logger(),"Contestant log write failed");}
  }
  void register_name() {std_msgs::msg::String m;m.data=Json{{"team",team_},{"name",name_}}.dump();registration_->publish(m);}
  void revise_action() {
    const auto &f=world_.current;
    if(!ai_action_revision_ || revised_tick_==f.tick || cached_tick_!=f.tick
      || cached_.empty() || f.tick>=max_ticks_)return;
    auto age=[&]{return std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-frame_received_).count();};
    if(age()>=revision_window_ms_)return;
    // The executor is single-threaded. Plan against the same legal frame,
    // without advancing observation history or changing the committed goal
    // unless a replacement will actually be sent.
    auto candidate=*planner_;auto action=candidate.decide(world_);
    if(!legal(action,f,stage_))return;
    auto replacement=action.json(f).dump();
    if(replacement==cached_ || age()>=revision_window_ms_)return;
    auto before=Json::parse(cached_);
    *planner_=std::move(candidate);cached_=replacement;revised_tick_=f.tick;
    std_msgs::msg::String message;message.data=cached_;action_pub_->publish(message);
    auto target=planner_->target();
    record({{"kind","action_revision"},{"session_id",f.session},{"tick",f.tick},
      {"before",before},{"action",action.json(f)},{"frame_age_ms",age()},
      {"reason",planner_->reason},{"goal_reason",planner_->goal_reason},{"target",target?Json::array({target->x,target->y}):Json()}});
    if(!planner_->purity_evaluations.empty()) record({{"kind","purity_evaluation"},{"tick",f.tick},
      {"revision",true},{"selected",action.json(f)},{"candidates",planner_->purity_evaluations}});
    if(!planner_->opening_route_evaluation.is_null()) record({{"kind","opening_route_evaluation"},{"tick",f.tick},
      {"revision",true},{"selected",action.json(f)},{"estimate",planner_->opening_route_evaluation}});
    if(!planner_->resource_route_evaluation.is_null()) record({{"kind","resource_route_evaluation"},{"tick",f.tick},
      {"revision",true},{"selected",action.json(f)},{"estimate",planner_->resource_route_evaluation}});
    if(!planner_->resource_race_evaluations.empty()) record({{"kind","resource_race_evaluation"},{"tick",f.tick},
      {"revision",true},{"selected",action.json(f)},{"estimates",planner_->resource_race_evaluations}});
    if(!planner_->resource_recovery_evaluation.is_null()) record({{"kind","resource_recovery_evaluation"},{"tick",f.tick},
      {"revision",true},{"selected",action.json(f)},{"estimate",planner_->resource_recovery_evaluation}});
  }
  void collect_ai(bool allow_revision=true) {
    if(!ai_)return;
    bool changed=false;
    for(auto &result:ai_->drain()) {
      if(result.session!=world_.current.session)continue;
      bool accepted=false,repair_queued=false;std::string schema_error;Json event_updates=Json::array();
      if(result.success && result.error.empty()) {
        try {
          auto before=world_.events.context();
          const Json *context=event_revisions_?&result.input.at("known_events"):nullptr;
          event_updates=world_.events.merge(result.parsed,world_.current,context);accepted=true;
          changed=changed || before!=world_.events.context();
        }
        catch(const std::exception &error) {
          result.error="model_schema_rejected";schema_error=model_schema_error_code(error);
          repair_queued=ai_->repair_schema(result,world_.current,world_.events,schema_error);
        }
      }
      record({{"kind","ai"},{"session_id",result.session},{"request_tick",result.tick},{"current_tick",world_.current.tick},
        {"http_status",result.http_status},{"http_success",result.success},{"accepted",accepted},{"error",result.error},
        {"schema_error",schema_error},{"repair_attempt",result.repair_attempt},{"repair_queued",repair_queued},
        {"interpretation_applied",accepted && use_event_interpretation_},
        {"elapsed_ms",result.elapsed_ms},
        {"model",result.model},{"reasoning_chars",result.reasoning_chars},{"usage",result.usage},
        {"event_updates",event_updates},
        {"event_memory",accepted?world_.events.context(event_revisions_):Json()}});
      if(result.success)ai_success_=true;
      if(accepted)RCLCPP_INFO(get_logger(),"AI event update: tick=%d events=%zu effects=%zu",world_.current.tick,world_.events.items.size(),world_.events.effects.size());
      else RCLCPP_WARN(get_logger(),"AI fallback: HTTP=%d reason=%s",result.http_status,result.error.c_str());
    }
    if(allow_revision && changed)revise_action();
  }
  std::string read_token() const {
    if(token_file_.empty())return env(deepseek_?"DEEPSEEK_API_KEY":"RMTC_TEAM_TOKEN");
    std::ifstream file(token_file_);std::string token;std::getline(file,token);
    if(!file && token.empty())throw std::runtime_error("cannot read token_file");
    if(!token.empty() && token.back()=='\r')token.pop_back();
    return token;
  }
  void perception(const std::string &raw) {
    const auto start=std::chrono::steady_clock::now();
    try {
      if(raw.size()>2*1024*1024)throw std::invalid_argument("perception exceeds size limit");
      auto f=parse_frame(Json::parse(raw));
      if(f.team!=team_)throw std::invalid_argument("perception team mismatch");
      bool changed=f.session!=world_.current.session;
      if(changed && retired_sessions_.count(f.session))return;
      if(!changed && !world_.history.empty() && f.tick<world_.current.tick)return;
      if(!changed && !world_.history.empty() && f.tick==world_.current.tick) {
        if(f.tick<max_ticks_ && !cached_.empty()) {std_msgs::msg::String m;m.data=cached_;action_pub_->publish(m);}
        return;
      }
      if(changed && !world_.current.session.empty())retired_sessions_.insert(world_.current.session);
      world_.update(f);
      frame_received_=start;revised_tick_=-1;cached_tick_=-1;
      if(changed) {
        planner_->reset();cached_.clear();ai_success_=false;qualification_tick_=-100;
        if(ai_) {
          std::string token;
          try {
            token=read_token();
            if(token.find_first_of("\r\n")!=std::string::npos || token.size()>8192)throw std::invalid_argument("invalid token");
          } catch(const std::exception &) {
            token.clear();RCLCPP_ERROR(get_logger(),"Cannot load a valid team token; continuing local control");
          }
          ai_->reset(f.session,token);
          if(token.empty())RCLCPP_WARN(get_logger(),"AI credential is missing; set %s or token_file",deepseek_?"DEEPSEEK_API_KEY":"RMTC_TEAM_TOKEN");
        }
        record({{"kind","session"},{"session_id",f.session},{"team",team_},{"stage",stage_}});
      }
      collect_ai(false);
      if(f.tick>=max_ticks_) {
        record({{"kind","final"},{"session_id",f.session},{"tick",f.tick},{"resource_value",f.resource_value},{"effective_damage",f.effective_damage},{"missions",f.completed},{"ai_http_success",ai_success_}});
        RCLCPP_INFO(get_logger(),"Finished: R=%.1f D=%.1f missions=%zu AI_http=%s",f.resource_value,f.effective_damage,f.completed.size(),ai_success_?"yes":"no");return;
      }
      if(ai_) {
        // The public protocol separates recent announcements with newlines.
        // Submit each raw notice once, instead of reinterpreting every growing
        // history batch. Meaning and parameters remain exclusively LLM output.
        std::istringstream notices(f.event_text);std::string notice;
        while(std::getline(notices,notice)) if(!notice.empty())
          ai_->submit(f,world_.events,notice,"perception");
        if(!ai_success_ && f.tick-qualification_tick_>=20) {
          if(ai_->submit(f,world_.events,mission_text_,"mission",true))qualification_tick_=f.tick;
        }
      }
      auto a=planner_->decide(world_);if(!legal(a,f,stage_))a=Action{};
      cached_=a.json(f).dump();cached_tick_=f.tick;
      std_msgs::msg::String msg;msg.data=cached_;action_pub_->publish(msg);
      double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      auto decoded=Json::parse(raw);
      Json observed_resources=Json::array(),observed_enemies=Json::array(),known_obstacles=Json::array();
      for(const auto &[pos,record]:world_.obstacles) {
        (void)record;known_obstacles.push_back({pos.x,pos.y});
      }
      for(const auto &e:f.entities) {
        if(e.type=="resource_normal" || e.type=="resource_high")
          observed_resources.push_back({{"id",e.id},{"position",{e.pos.x,e.pos.y}},{"value",e.value}});
        if(e.type=="robot") observed_enemies.push_back({{"team",e.team},{"position",{e.pos.x,e.pos.y}},
          {"hp",e.hp},{"defending",e.defending},{"protected",e.protected_spawn}});
      }
      auto target=planner_->target();
      if(!planner_->purity_evaluations.empty()) record({{"kind","purity_evaluation"},{"tick",f.tick},
        {"selected",a.json(f)},{"candidates",planner_->purity_evaluations}});
      if(!planner_->opening_route_evaluation.is_null()) record({{"kind","opening_route_evaluation"},{"tick",f.tick},
        {"selected",a.json(f)},{"estimate",planner_->opening_route_evaluation}});
      if(!planner_->resource_route_evaluation.is_null()) record({{"kind","resource_route_evaluation"},{"tick",f.tick},
        {"selected",a.json(f)},{"estimate",planner_->resource_route_evaluation}});
      if(!planner_->resource_race_evaluations.empty()) record({{"kind","resource_race_evaluation"},{"tick",f.tick},
        {"selected",a.json(f)},{"estimates",planner_->resource_race_evaluations}});
      if(!planner_->resource_recovery_evaluation.is_null()) record({{"kind","resource_recovery_evaluation"},{"tick",f.tick},
        {"selected",a.json(f)},{"estimate",planner_->resource_recovery_evaluation}});
      if(!planner_->observation_evaluation.is_null()) record({{"kind","observation_evaluation"},
        {"session_id",f.session},{"tick",f.tick},{"selected",a.json(f)},{"estimate",planner_->observation_evaluation}});
      record({{"kind","decision"},{"session_id",f.session},{"tick",f.tick},{"position",{f.pos.x,f.pos.y}},
        {"hp",f.hp},{"alive",f.alive},{"protected",f.protected_spawn},
        {"public_missions",f.missions},{"initial_spawn",world_.spawn?Json::array({world_.spawn->x,world_.spawn->y}):Json()},
        {"action",a.json(f)},{"reason",planner_->reason},{"goal_reason",planner_->goal_reason},{"last_action",decoded["self"].value("last_action",Json())},
        {"resource_value",f.resource_value},{"effective_damage",f.effective_damage},{"completed",f.completed},{"latency_ms",elapsed},
        {"target",target?Json::array({target->x,target->y}):Json()},
        {"observed_resources",observed_resources},{"observed_enemies",observed_enemies},
        {"known_obstacles",known_obstacles},{"map_size",{f.width,f.height}},{"vision_radius",f.radius},
        {"known_resources",world_.resources.size()},{"explored_cells",world_.explored.size()}});
    } catch(const std::exception &) {
      // Never guess a session or Tick after malformed input. Error bodies may contain secrets.
      RCLCPP_WARN_THROTTLE(get_logger(),*get_clock(),2000,"Perception rejected; waiting for a valid frame");
      record({{"kind","perception_rejected"}});
    }
  }
  std::string team_,name_,token_file_,mission_text_,cached_;
  std::set<std::string> retired_sessions_;
  int stage_{},max_ticks_{},qualification_tick_{-100};
  int revision_window_ms_{350},cached_tick_{-1},revised_tick_{-1};
  std::chrono::steady_clock::time_point frame_received_{};
  bool ai_action_revision_{},use_event_interpretation_{true},event_revisions_{};
  bool ai_enabled_{},ai_success_{},log_failed_{},deepseek_{};
  World world_;std::unique_ptr<Planner> planner_;std::unique_ptr<AiClient> ai_;std::ofstream log_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr action_pub_,registration_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr perception_,mission_,notices_,registry_;
  rclcpp::TimerBase::SharedPtr register_timer_,ai_timer_;
};
} // namespace rmtc
int main(int argc,char **argv) {
  rclcpp::init(argc,argv);
  try {rclcpp::spin(std::make_shared<rmtc::Contestant>());}
  catch(const std::exception &e) {RCLCPP_ERROR(rclcpp::get_logger("rmtc_contestant"),"Startup/runtime failure: %s",e.what());rclcpp::shutdown();return 1;}
  rclcpp::shutdown();return 0;
}
