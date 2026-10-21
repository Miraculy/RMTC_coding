#include "rmtc_bot/ai.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>

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
    planner_=std::make_unique<Planner>(stage_,max_ticks_);
    token_file_=declare_parameter<std::string>("token_file","");
    AiConfig config;
    config.url=declare_parameter<std::string>("ai_gateway",env("AI_GATEWAY","http://127.0.0.1:8080/v1/chat/completions"));
    config.model=declare_parameter<std::string>("ai_model",env("RMTC_AI_MODEL"));
    config.timeout_ms=declare_parameter<int>("ai_timeout_ms",12000);
    config.budget=declare_parameter<int>("ai_budget",90);
    config.max_tokens=declare_parameter<int>("ai_max_tokens",2500);
    ai_enabled_=declare_parameter<bool>("ai_enabled",true) && stage_==2;
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
  void collect_ai() {
    if(!ai_)return;
    for(auto &result:ai_->drain()) {
      if(result.session!=world_.current.session)continue;
      bool accepted=false;
      if(result.success && result.error.empty()) {
        try {world_.events.merge(result.parsed,world_.current);accepted=true;}
        catch(const std::exception &) {result.error="model_schema_rejected";}
      }
      record({{"kind","ai"},{"session_id",result.session},{"request_tick",result.tick},{"current_tick",world_.current.tick},
        {"http_status",result.http_status},{"http_success",result.success},{"accepted",accepted},{"error",result.error},{"elapsed_ms",result.elapsed_ms},
        {"event_memory",accepted?world_.events.context():Json()}});
      if(result.success)ai_success_=true;
      if(accepted)RCLCPP_INFO(get_logger(),"AI event update: tick=%d events=%zu effects=%zu",world_.current.tick,world_.events.items.size(),world_.events.effects.size());
      else RCLCPP_WARN(get_logger(),"AI fallback: HTTP=%d reason=%s",result.http_status,result.error.c_str());
    }
  }
  std::string read_token() const {
    if(token_file_.empty())return env("RMTC_TEAM_TOKEN");
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
          if(token.empty())RCLCPP_WARN(get_logger(),"RMTC_TEAM_TOKEN is missing; official mission eligibility requires a successful match-specific AI call");
        }
        record({{"kind","session"},{"session_id",f.session},{"team",team_},{"stage",stage_}});
      }
      collect_ai();
      if(f.tick>=max_ticks_) {
        record({{"kind","final"},{"session_id",f.session},{"tick",f.tick},{"resource_value",f.resource_value},{"effective_damage",f.effective_damage},{"missions",f.completed},{"ai_http_success",ai_success_}});
        RCLCPP_INFO(get_logger(),"Finished: R=%.1f D=%.1f missions=%zu AI_http=%s",f.resource_value,f.effective_damage,f.completed.size(),ai_success_?"yes":"no");return;
      }
      if(ai_) {
        if(!f.event_text.empty())ai_->submit(f,world_.events,f.event_text,"perception");
        if(!ai_success_ && f.tick-qualification_tick_>=20) {
          if(ai_->submit(f,world_.events,mission_text_,"mission",true))qualification_tick_=f.tick;
        }
      }
      auto a=planner_->decide(world_);if(!legal(a,f,stage_))a=Action{};
      cached_=a.json(f).dump();std_msgs::msg::String msg;msg.data=cached_;action_pub_->publish(msg);
      double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      auto decoded=Json::parse(raw);
      record({{"kind","decision"},{"session_id",f.session},{"tick",f.tick},{"position",{f.pos.x,f.pos.y}},
        {"hp",f.hp},{"action",a.json(f)},{"reason",planner_->reason},{"last_action",decoded["self"].value("last_action",Json())},
        {"resource_value",f.resource_value},{"effective_damage",f.effective_damage},{"completed",f.completed},{"latency_ms",elapsed}});
    } catch(const std::exception &) {
      // Never guess a session or Tick after malformed input. Error bodies may contain secrets.
      RCLCPP_WARN_THROTTLE(get_logger(),*get_clock(),2000,"Perception rejected; waiting for a valid frame");
      record({{"kind","perception_rejected"}});
    }
  }
  std::string team_,name_,token_file_,mission_text_,cached_;
  std::set<std::string> retired_sessions_;
  int stage_{},max_ticks_{},qualification_tick_{-100};
  bool ai_enabled_{},ai_success_{},log_failed_{};
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
