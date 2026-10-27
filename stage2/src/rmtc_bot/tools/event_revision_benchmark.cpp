// Explicit paid development fixture. Synthetic public notices and seeded bad
// model memory test revision semantics; this is not a match or score result.
#include "rmtc_bot/ai.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace rmtc;
int main(int argc,char **argv) {
  if(argc!=3 || std::string(argv[1])!="--live-gateway") {
    std::cerr<<"Opt-in paid test: event_revision_benchmark --live-gateway URL\n";return 2;
  }
  const char *token=std::getenv("RMTC_TEAM_TOKEN");if(!token || !*token)return 2;
  AiConfig config;config.url=argv[2];config.provider="deepseek";config.model="deepseek-flash";
  config.event_revisions=true;config.budget=12;AiClient client(config);
  int passed=0;
  for(int scenario=0;scenario<4;++scenario) {
    Frame f;f.session="event-revision-fixture-"+std::to_string(scenario);f.width=f.height=24;f.team="A";f.tick=5;
    client.reset(f.session,token);Events events;
    Json old{{"kind",scenario==0?"unrivaled":scenario==2?"world_split":"explosion"},
             {"start_tick",41},{"end_tick",scenario==0?91:scenario==2?71:42},{"params",Json::object()}};
    if(scenario==2)old["points"]={{10,11}};else old["rect"]=scenario==0?Json{{10,10},{14,14}}:Json{{3,3},{5,5}};
    events.merge({{"session_id",f.session},{"events",Json::array({old})},{"effects",Json::array()}},f);
    const std::string notices[]{
      "第5 Tick：再次确认此前预告的同一座天下无双堡垒：Tick41在中心[12,12]出现，范围为3乘3，持续50 Tick，连续驻留10 Tick获得增益。5乘5指攻击范围，不是堡垒范围。",
      "第5 Tick：此前预告在Tick41发生的那次爆炸，准确矩形是[4,4]至[6,6]，造成80HP，只有这一次；请修正此前对范围的理解。",
      "第5 Tick：此前预告Tick41发动的同一次世界分裂，其交点应为[11,10]，不是[10,11]；十字墙持续至Tick71。",
      "第5 Tick：除了已预告在Tick41于[3,3]至[5,5]发生的爆炸，还有另一场独立爆炸，也在Tick41，矩形为[4,4]至[6,6]，造成80HP。两场同时存在。"};
    if(!client.submit(f,events,notices[scenario],"perception"))return 3;
    AiResult result;bool received=false;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
    while(std::chrono::steady_clock::now()<deadline && !received) {
      auto rows=client.drain();
      for(auto &row:rows) {
        result=std::move(row);
        if(result.success || (result.http_status!=0 && result.http_status!=502 && result.http_status!=504))received=true;
      }
      if(!received)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool schema=false,correct=false;Json changes=Json::array();std::string error=result.error;
    if(received && result.success && result.error.empty())try {
      changes=events.merge(result.parsed,f,&result.input.at("known_events"));schema=true;
      const auto state=events.context(true)["events"];
      if(scenario==0)correct=state.size()==1 && state[0]["rect"]==Json{{11,11},{13,13}}
        && state[0]["start_tick"]==41 && state[0]["end_tick"]==91 && state[0]["revision"]==1;
      if(scenario==1)correct=state.size()==1 && state[0]["rect"]==Json{{4,4},{6,6}}
        && state[0]["start_tick"]==41 && state[0]["end_tick"]==42 && state[0]["revision"]==1;
      if(scenario==2)correct=state.size()==1 && state[0]["points"]==Json{{11,10}}
        && state[0]["start_tick"]==41 && state[0]["end_tick"]==71 && state[0]["revision"]==1;
      if(scenario==3)correct=state.size()==2 && state[0]["rect"]==Json{{3,3},{5,5}}
        && state[1]["rect"]==Json{{4,4},{6,6}} && state[0]["revision"]==0 && state[1]["revision"]==0;
    } catch(const std::exception &e) {error=model_schema_error_code(e);}
    bool ok=received && schema && correct && result.reasoning_chars==0;passed+=ok;
    std::cout<<Json{{"scenario",scenario},{"passed",ok},{"schema",schema},{"correct",correct},
      {"http_status",result.http_status},{"error",error},{"elapsed_ms",result.elapsed_ms},
      {"reasoning_chars",result.reasoning_chars},{"updates",changes},{"memory",events.context(true)}}.dump()<<std::endl;
  }
  std::cout<<Json{{"samples",4},{"passed",passed},{"synthetic_fixture",true}}.dump()<<std::endl;
  return passed==4?0:1;
}
