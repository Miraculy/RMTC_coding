#include "rmtc_bot/ai.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>

using namespace rmtc;
namespace {
struct Sample {std::string name,text,kind;int start,end;Json detail;bool effect{false};};
bool matches(const Json &actual,const Json &expected) {
  if(!expected.is_object())return actual==expected;
  if(!actual.is_object())return false;
  for(auto it=expected.begin();it!=expected.end();++it)
    if(!actual.contains(it.key()) || !matches(actual.at(it.key()),it.value()))return false;
  return true;
}
}
int main(int argc,char **argv) {
  if(argc!=2 || std::string(argv[1])!="--live-deepseek") {
    std::cerr<<"Opt-in paid test: ai_benchmark --live-deepseek (DEEPSEEK_API_KEY or stdin)\n";return 2;
  }
  std::string token;const auto *key=std::getenv("DEEPSEEK_API_KEY");
  if(key)token=key;else std::getline(std::cin,token);
  if(token.empty() || token.find_first_of("\r\n")!=std::string::npos)return 2;
  AiConfig config;config.provider="deepseek";config.url="https://api.deepseek.com/chat/completions";
  config.model="deepseek-flash";config.timeout_ms=12000;config.budget=16;
  if(const auto *model=std::getenv("RMTC_AI_MODEL"))config.model=model;
  AiClient client(config);Frame frame;frame.session="deepseek-benchmark";frame.team="A";frame.width=frame.height=24;frame.tick=40;
  client.reset(frame.session,token);token.clear();
  std::vector<Sample> samples{
    {"qualification","本局任务：累计完成两次普通攻击、一次高额攻击，并到达坐标(12,12)。当前没有随机事件。","",0,0,Json::object()},
    {"explosion","预警：Tick 45 时，矩形(3,4)到(7,8)（包含边界）将发生一次爆炸，造成80点伤害。","explosion",45,46,{{"rect",{{3,4},{7,8}}},{"params",{{"damage",80}}}}},
    {"illusion","Tick 50 激活幻术，范围为(2,2)至(6,6)。激活时位于区域内的机器人移动方向反转50 Tick，攻击方向不变，事件在Tick 100结束。","illusion",50,100,{{"rect",{{2,2},{6,6}}},{"params",{{"effect_ticks",50}}}}},
    {"angel","回血点(8,9)从Tick 60存在至Tick 80（不含80）。机器人成功走入即可获得10 Tick回血，每Tick恢复10生命，生命上限100。","angel",60,80,{{"points",{{8,9}}},{"params",{{"effect_ticks",10},{"heal_per_tick",10},{"hp_cap",100}}}}},
    {"unrivaled","无双堡垒从Tick 70到Tick 120有效，中心(12,12)，大小3乘3。连续在战斗后驻留10 Tick可获得无双：攻击覆盖自身周围5乘5区域，伤害50，防御后伤害10。","unrivaled",70,120,{{"rect",{{11,11},{13,13}}},{"params",{{"stay_ticks",10},{"attack_radius",2},{"attack_damage",50},{"defended_damage",10}}}}},
    {"world_split","世界分割在Tick 80发动，以(10,11)为交点的整行整列成为临时墙体；Tick 110墙体移除。","world_split",80,110,{{"points",{{10,11}}}}},
    {"pepper","辣椒点(4,5)和(14,15)在Tick 90出现，在Tick 120消失。机器人成功进入辣椒点会在本Tick点燃该行该列，环境伤害50。","pepper",90,120,{{"points",{{4,5},{14,15}}},{"params",{{"damage",50}}}}},
    {"purity","纯洁之域全局事件从Tick 100持续至Tick 120。在此期间，攻击被防御时，攻击者受到20点反伤。","purity",100,120,{{"params",{{"reflection_damage",20}}}}},
    {"pan","平底锅全局事件Tick 130激活，Tick 160结束。前20 Tick成功移动的机器人从下一Tick战斗起易伤，受到伤害乘2；所有易伤统一在Tick 160结束。","pan",130,160,{{"params",{{"movement_window_ticks",20},{"vulnerability_ticks",30},{"damage_multiplier",2}}}}},
    {"pan_effect","A队在Tick 135成功移动，获得平底锅易伤。首次受影响的战斗为Tick 136，效果在Tick 160统一结束，受到伤害乘2。","pan",136,160,{{"team","A"},{"params",{{"damage_multiplier",2}}}},true},
    {"explosion_repeat_1","爆炸预警：矩形(3,4)到(7,8)，Tick 45一次性造成80伤害。","explosion",45,46,{{"rect",{{3,4},{7,8}}},{"params",{{"damage",80}}}}},
    {"explosion_repeat_2","将在Tick 45炸毁(3,4)至(7,8)内的机器人80点生命，仅在该Tick结算一次爆炸。","explosion",45,46,{{"rect",{{3,4},{7,8}}},{"params",{{"damage",80}}}}}
  };
  Json rows=Json::array();std::vector<long> times;int passed=0;
  for(const auto &sample:samples) {
    const auto begin=std::chrono::steady_clock::now();
    if(!client.submit(frame,Events{},sample.text,"perception"))return 3;
    const auto submit_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
    AiResult result;bool received=false;
    auto deadline=begin+std::chrono::seconds(40);
    while(std::chrono::steady_clock::now()<deadline) {
      auto batch=client.drain();
      for(auto &r:batch) {
        result=std::move(r);
        // The client may retry transport/upstream failures; wait for its terminal result.
        if(result.success || (result.http_status!=0 && result.http_status!=502 && result.http_status!=504))received=true;
      }
      if(received)break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    bool schema=false,correct=false;
    if(result.success && result.error.empty()) {
      try {
        Events events;events.merge(result.parsed,frame);schema=true;
        const auto &records=result.parsed.at(sample.effect?"effects":"events");
        if(sample.kind.empty())correct=records.empty() && result.parsed.at("effects").empty();
        else for(const auto &record:records) {
          Json expected=sample.detail;expected["kind"]=sample.kind;expected["start_tick"]=sample.start;expected["end_tick"]=sample.end;
          if(matches(record,expected))correct=true;
        }
      } catch(...) {}
    }
    const bool ok=received && schema && correct && result.reasoning_chars==0;
    passed+=ok;times.push_back(result.elapsed_ms);
    Json row{{"sample",sample.name},{"http_status",result.http_status},{"elapsed_ms",result.elapsed_ms},
      {"end_to_end_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count()},
      {"submit_us",submit_us},{"schema_valid",schema},{"expected_fields_correct",correct},{"passed",ok},
      {"reasoning_chars",result.reasoning_chars},{"model",result.model},{"error",result.error},{"usage",result.usage},{"parsed",result.parsed}};
    rows.push_back(row);std::cout<<row.dump()<<std::endl;
    if(result.http_status==401 || result.http_status==402 || !received)break;
  }
  std::sort(times.begin(),times.end());
  Json summary{{"kind","summary"},{"samples",rows.size()},{"passed",passed},{"model_requested",config.model},
    {"mean_ms",times.empty()?0:std::accumulate(times.begin(),times.end(),0.0)/times.size()},
    {"median_ms",times.empty()?0:(times[(times.size()-1)/2]+times[times.size()/2])/2.0},
    {"min_ms",times.empty()?0:times.front()},{"max_ms",times.empty()?0:times.back()}};
  std::cout<<summary.dump()<<std::endl;
  return passed==static_cast<int>(samples.size())?0:1;
}
