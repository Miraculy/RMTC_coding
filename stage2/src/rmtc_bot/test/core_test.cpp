#include "rmtc_bot/core.hpp"
#include <gtest/gtest.h>
using namespace rmtc;
namespace {
std::string grid(int dx,int dy) {
  std::string s;
  for(int y=0;y<9;++y){for(int x=0;x<9;++x)s+=(x==4 && y==4)?'1':(x==4+dx && y==4+dy)?'2':'0';if(y<8)s+='\n';}return s;
}
Json perception(int tick=0) {
  return {{"tick",tick},{"session_id","test-match"},{"vision_radius",4},
    {"map",{{"width",24},{"height",24}}},
    {"self",{{"team","A"},{"position",{8,8}},{"hp",100},{"alive",true},{"protected",false},{"last_action",nullptr}}},
    {"visible_entities",Json::array()},
    {"score",{{"resource_value",0},{"effective_damage",0},{"mission_completed_ids",Json::array()}}},
    {"missions",Json::array()},{"event_text",""}};
}
Frame frame(int tick=0) { return parse_frame(perception(tick)); }
World exploration_race_world(int tick=20,bool with_rival=true) {
  World w;auto f=frame(tick);
  if(with_rival) f.entities.push_back({"robot","B","B",{4,8},100,0,false,true});
  w.update(f);
  // Two equal unseen strips; the protected rival can collect but cannot
  // distract the component test with a combat goal.
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y)
    if(x!=3 && x!=13) w.explored.insert({x,y});
  return w;
}
World revival_route_world(int current_tick=490,bool with_resource=false) {
  World w;
  for(int tick=current_tick-32;tick<=current_tick;++tick) {
    auto f=frame(tick);f.alive=tick<current_tick-3 || tick==current_tick;
    f.hp=f.alive?(tick==current_tick?100:20):0;f.protected_spawn=tick==current_tick;
    f.effective_damage=tick<current_tick-3?360:300;
    f.missions.push_back({{"id","reach"},{"type","reach"},{"position",{12,12}}});
    if(f.alive) {
      f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});
      if(with_resource) f.entities.push_back({"resource_high","h","",{9,8},100,20,false,false});
    }
    w.update(f);
  }
  for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
  return w;
}
World fire_choice_world(int tick=200,bool block_down=false) {
  World w;auto f=frame(tick);f.pos={16,5};f.hp=40;f.effective_damage=200;
  f.entities.push_back({"robot","B","B",{14,4},80,0,false,false});
  f.entities.push_back({"robot","C","C",{18,4},100,0,false,false});
  if(block_down) f.entities.push_back({"obstacle","","",{16,6},100,0,false,false});
  f.missions.push_back({{"id","reach"},{"type","reach"},{"position",{14,7}}});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  return w;
}
Json batch(const Frame &f,Json events=Json::array(),Json effects=Json::array()) {
  return {{"session_id",f.session},{"events",events},{"effects",effects}};
}
Json area(const std::string &kind,int start,int end,Json params=Json::object()) {
  return {{"kind",kind},{"start_tick",start},{"end_tick",end},{"rect",{{4,4},{9,9}}},{"params",params}};
}
World purity_trade_world(double hp=25,double enemy_hp=20) {
  World w;auto f=frame(200);f.hp=hp;f.effective_damage=80;
  f.entities.push_back({"robot","B","B",{10,8},enemy_hp,0,true,false});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  w.events.merge(batch(f,Json::array({area("purity",200,220,{{"reflection_damage",20}})})),f);
  return w;
}
World opening_route_world(int tick=5) {
  World w;auto f=frame(tick);
  f.entities.push_back({"resource_normal","normal","",{10,10},100,10,false,false});
  f.missions.push_back({{"id","normal"},{"type","normal_resources"},{"target",1}});w.update(f);
  // The right-hand strip was already seen; moving down can reveal nine cells.
  for(int y=4;y<=12;++y) w.explored.insert({13,y});
  return w;
}
World resource_pickup_world(int tick=5) {
  World w;auto f=frame(tick);
  f.entities.push_back({"resource_normal","near","",{8,10},100,10,false,false});
  f.entities.push_back({"resource_high","goal","",{10,10},100,20,false,false});
  f.missions.push_back({{"id","high"},{"type","high_resources"},{"target",1}});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  return w;
}
World resource_race_world() {
  World w;
  for(int tick=10;tick<=11;++tick) {
    auto f=frame(tick);
    f.entities.push_back({"resource_high","raced","",{10,8},100,20,false,false});
    f.entities.push_back({"resource_normal","other","",{8,11},100,10,false,false});
    f.entities.push_back({"robot","B","B",{tick==10?12:11,8},100,0,false,false});
    f.missions.push_back({{"id","high"},{"type","high_resources"},{"target",1}});
    f.missions.push_back({{"id","normal"},{"type","normal_resources"},{"target",2}});
    w.update(f);
  }
  // Use the actual opening footprint and the public collection tasks. A fully
  // explored synthetic map instead activates optional hunting before Tick 100.
  return w;
}
}
TEST(Protocol, RelativeWorldAxesAndOverlap) {
  EXPECT_EQ(relative_position(grid(1,-2)),(Pos{1,-2}));
  EXPECT_EQ(relative_position(grid(0,0)),(Pos{0,0}));
  auto bad=grid(1,1);bad[0]='1';EXPECT_THROW(relative_position(bad),std::exception);
  EXPECT_THROW(relative_position("000"),std::exception);
}
TEST(Protocol, StrictTypesAndEntityCoordinates) {
  auto j=perception();j["visible_entities"].push_back({{"type","resource_high"},{"id","h"},{"position",grid(1,-2)},{"value",20}});
  EXPECT_EQ(parse_frame(j).entities.front().pos,(Pos{9,6}));
  j["tick"]=true;EXPECT_THROW(parse_frame(j),std::exception);
  j["tick"]=0;j["self"]["position"]="[8,8]";EXPECT_THROW(parse_frame(j),std::exception);
}
TEST(Protocol, RadiusDoesNotChangeGridSize) {
  auto j=perception();j["vision_radius"]=1;
  j["visible_entities"].push_back({{"type","obstacle"},{"position",grid(2,0)}});
  EXPECT_THROW(parse_frame(j),std::exception);
}
TEST(Protocol, ActionStateAndStageValidation) {
  auto f=frame();EXPECT_TRUE(legal({"move","up"},f,1));EXPECT_FALSE(legal({"attack","up"},f,1));
  f.protected_spawn=true;EXPECT_FALSE(legal({"attack","up"},f,2));
  f.alive=false;EXPECT_FALSE(legal({"defend",""},f,2));EXPECT_TRUE(legal({},f,2));
  EXPECT_EQ(Action({"move","right"}).json(f)["session_id"],f.session);
}
TEST(World, DuplicateOldAndSessionReset) {
  World w;auto f=frame();ASSERT_TRUE(w.update(f));EXPECT_FALSE(w.update(f));
  f.tick=2;w.update(f);f.tick=1;EXPECT_FALSE(w.update(f));
  f.session="next";f.tick=0;EXPECT_TRUE(w.update(f));EXPECT_EQ(w.history.size(),1u);EXPECT_TRUE(w.events.items.empty());
}
TEST(World, ObservedResourceDisappearanceAndAuthoritativePosition) {
  World w;auto f=frame();f.entities.push_back({"resource_normal","n","",{9,8},100,10,false,false});
  w.update(f);ASSERT_EQ(w.resources.size(),1u);
  f.tick=1;f.entities.clear();f.pos={8,9};f.moved=false;w.update(f);
  EXPECT_TRUE(w.resources.empty());EXPECT_EQ(w.current.pos,(Pos{8,9}));
}
TEST(World, InitialSpawnSurvivesFallbackRespawnsAndUnknownOriginRemainsUnknown) {
  World w;auto j=perception();j["self"]["position_reason"]="initial";w.update(parse_frame(j));
  ASSERT_EQ(w.spawn,std::optional<Pos>(Pos{8,8}));
  j["tick"]=1;j["self"]["alive"]=false;j["self"]["hp"]=0;j["self"]["position"]={13,9};
  j["self"].erase("position_reason");w.update(parse_frame(j));
  j["tick"]=5;j["self"]["alive"]=true;j["self"]["hp"]=100;j["self"]["protected"]=true;
  j["self"]["position"]={8,7};j["self"]["position_reason"]="respawn";w.update(parse_frame(j));
  EXPECT_EQ(w.current.pos,(Pos{8,7}));EXPECT_EQ(w.spawn,std::optional<Pos>(Pos{8,8}));
  j["tick"]=9;j["self"]["position"]={9,8};w.update(parse_frame(j));
  EXPECT_EQ(w.current.pos,(Pos{9,8}));EXPECT_EQ(w.spawn,std::optional<Pos>(Pos{8,8}));
  // Joining another session after Tick0 does not reveal its initial origin.
  j["session_id"]="late-join";j["tick"]=12;j["self"]["position"]={2,3};w.update(parse_frame(j));
  EXPECT_FALSE(w.spawn);EXPECT_EQ(w.current.pos,(Pos{2,3}));
  j["session_id"]="new-match";j["tick"]=0;j["self"]["position_reason"]="initial";w.update(parse_frame(j));
  EXPECT_EQ(w.spawn,std::optional<Pos>(Pos{2,3}));
}
TEST(World, ResourceTypeProgressRequiresConfirmedOwnCollection) {
  World w;auto f=frame();f.entities.push_back({"resource_normal","n","",{9,8},100,10,false,false});
  w.update(f);f.tick=1;f.pos={9,8};f.resource_value=10;f.entities.clear();w.update(f);
  EXPECT_EQ(w.normal_collected,1);EXPECT_EQ(w.high_collected,0);
  f.tick=3;f.resource_value=30;w.update(f); // Missing frame/type cannot be reconstructed.
  EXPECT_EQ(w.normal_collected,1);EXPECT_EQ(w.high_collected,0);
  f.session="reset";f.tick=0;w.update(f);EXPECT_EQ(w.normal_collected,0);
}
TEST(World, CompetitionCueComesOnlyFromRecentOwnObservation) {
  World w;auto f=frame();f.entities.push_back({"resource_normal","n","",{9,8},100,10,false,false});
  w.update(f);f.tick=1;f.entities.clear();f.entities.push_back({"robot","B","B",{9,8},100,0,false,false});
  w.update(f);ASSERT_EQ(w.recent_competition.size(),1u);EXPECT_EQ(w.recent_competition.at("B"),1);
  EXPECT_EQ(f.resource_value,0);f.session="reset";f.tick=0;w.update(f);EXPECT_TRUE(w.recent_competition.empty());
}
TEST(ScoreV11, UsesFiveHundredThresholdAndRewardsBeforeDeathFloor) {
  EXPECT_DOUBLE_EQ(combat_score(300),9);EXPECT_DOUBLE_EQ(combat_score(500),15);
  EXPECT_DOUBLE_EQ(combat_score(900),15);
  EXPECT_DOUBLE_EQ(projected_damage(80,20,60,0),160);
  EXPECT_DOUBLE_EQ(projected_damage(160,0,0,1),100);
  EXPECT_DOUBLE_EQ(projected_damage(0,10,60,1),10); // Mutual kill still adds reward first.
  EXPECT_DOUBLE_EQ(projected_damage(10,5,0,1),0);
  EXPECT_DOUBLE_EQ(projected_damage(0,5,30,0),35); // A half share of one kill reward.
}
TEST(World, AdjustedDamageCanFallWithoutLosingCompletedTasksOrDoubleDeduction) {
  auto j=perception(10);j["score"]["effective_damage"]=560;
  j["score"]["mission_completed_ids"]=Json::array({"damage_task"});
  World w;w.update(parse_frame(j));j["tick"]=11;j["score"]["effective_damage"]=500;
  j["self"]["alive"]=false;j["self"]["hp"]=0;w.update(parse_frame(j));
  EXPECT_DOUBLE_EQ(w.current.effective_damage,500);EXPECT_TRUE(w.current.completed.count("damage_task"));
  j["tick"]=15;j["self"]["alive"]=true;j["self"]["hp"]=100;w.update(parse_frame(j));
  EXPECT_DOUBLE_EQ(w.current.effective_damage,500);EXPECT_TRUE(w.current.completed.count("damage_task"));
}
TEST(Planner, ScoreV11ValuesVisibleFinisherAndPreservesFirstStage) {
  World w;auto f=frame(10);f.entities.push_back({"robot","B","B",{8,10},15,0,false,false});
  f.entities.push_back({"resource_high","h","",{9,8},100,20,false,false});w.update(f);
  PlannerConfig config;config.score_aware_combat=true;Planner candidate(2,500,config);
  auto a=candidate.decide(w);EXPECT_EQ(a.type,"attack");EXPECT_EQ(a.direction,"down");
  Planner baseline(1,100),stage1(1,100,config);auto expected=baseline.decide(w);a=stage1.decide(w);
  EXPECT_EQ(a.type,expected.type);EXPECT_EQ(a.direction,expected.direction);
}
TEST(Planner, ScoreV11BuildsDeathBufferWithoutChangingActualScoreFormula) {
  World w;auto f=frame(200);f.effective_damage=505;
  f.entities.push_back({"robot","B","B",{8,10},80,0,false,false});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=3;y<f.height;++y) w.explored.insert({x,y});
  PlannerConfig config;config.score_aware_combat=true;config.damage_reserve=0;Planner no_reserve(2,500,config);
  EXPECT_EQ(no_reserve.decide(w).type,"defend");
  config.damage_reserve=120;Planner reserved(2,500,config);auto a=reserved.decide(w);
  EXPECT_EQ(a.type,"attack");EXPECT_EQ(a.direction,"down");EXPECT_DOUBLE_EQ(combat_score(620),15);
  f.tick=498;w.update(f);Planner last(2,500,config);
  config.damage_reserve=0;Planner last_no_reserve(2,500,config);
  auto expected=last_no_reserve.decide(w);a=last.decide(w);EXPECT_EQ(a.type,expected.type);EXPECT_EQ(a.direction,expected.direction);
  config.damage_reserve=-1;EXPECT_THROW(Planner(2,500,config),std::exception);
  config.damage_reserve=601;EXPECT_THROW(Planner(2,500,config),std::exception);
}
TEST(Events, AtomicSchemaAndSessionValidation) {
  World w;w.update(frame());auto valid=area("explosion",50,51,{{"damage",35}});
  auto bad=valid;bad["kind"]="unknown";
  EXPECT_THROW(w.events.merge(batch(w.current,Json::array({valid,bad})),w.current),std::exception);
  EXPECT_TRUE(w.events.items.empty());
  auto wrong=batch(w.current);wrong["session_id"]="old";EXPECT_THROW(w.events.merge(wrong,w.current),std::exception);
}
TEST(Events, ModelFailureDiagnosticsClassifyWithoutEchoingUntrustedText) {
  auto f=frame();Events events;auto invalid=area("explosion",20,21);invalid.erase("rect");
  try {events.merge(batch(f,Json::array({invalid})),f);FAIL()<<"missing rectangle accepted";}
  catch(const std::exception &error) {EXPECT_EQ(model_schema_error_code(error),"missing_rectangle");}
  try {Json::object().at("sensitive-example");FAIL()<<"missing field accepted";}
  catch(const std::exception &error) {EXPECT_EQ(model_schema_error_code(error),"json_missing_field");}
  EXPECT_EQ(model_schema_error_code(std::invalid_argument("sensitive-example")),"validation_error");
}
TEST(Events, DeduplicatesSemanticEventAndKeepsDifferentSameTickEvents) {
  auto f=frame();Events events;
  auto j=batch(f,Json::array({area("explosion",20,21),area("illusion",20,21)}));
  events.merge(j,f);events.merge(j,f);EXPECT_EQ(events.items.size(),2u);
  auto other=area("explosion",20,21);other["rect"]={{1,1},{2,2}};
  events.merge(batch(f,Json::array({other})),f);EXPECT_EQ(events.items.size(),3u);
}
TEST(Events, ExplicitRevisionRemovesOldFortressGeometryAndInferredBuff) {
  World w;auto f=frame(10);f.pos={6,6};w.update(f);
  auto fortress=area("unrivaled",10,30,{{"stay_ticks",2}});fortress["rect"]={{6,6},{10,10}};
  w.events.merge(batch(f,Json::array({fortress})),f);
  f.tick=11;w.update(f);f.tick=12;w.update(f);ASSERT_TRUE(w.effect("unrivaled","A",12));
  auto known=w.events.context(true);auto correction=known["events"][0];correction["rect"]={{7,7},{9,9}};
  auto changes=w.events.merge(batch(f,Json::array({correction})),f,&known);
  ASSERT_EQ(w.events.items.size(),1u);ASSERT_EQ(changes.size(),1u);
  EXPECT_EQ(changes[0]["kind"],"event_revision");EXPECT_EQ(w.events.items[0].revision,1);
  EXPECT_FALSE(w.effect("unrivaled","A",12));
  EXPECT_EQ(w.events.context(true)["events"][0]["event_id"],known["events"][0]["event_id"]);
  EXPECT_FALSE(w.events.context()["events"][0].contains("event_id"));
}
TEST(Events, RevisingOneExplosionPreservesIndependentOverlappingExplosion) {
  World w;auto f=frame(10);w.update(f);
  auto first=area("explosion",20,21,{{"damage",80}});first["rect"]={{7,7},{9,9}};
  auto second=area("explosion",20,21,{{"damage",35}});second["rect"]={{8,8},{10,10}};
  w.events.merge(batch(f,Json::array({first,second})),f);
  EXPECT_DOUBLE_EQ(w.environmental_damage({8,8},20,false),115);
  auto known=w.events.context(true);auto correction=known["events"][0];correction["rect"]={{0,0},{1,1}};
  w.events.merge(batch(f,Json::array({correction})),f,&known);
  EXPECT_EQ(w.events.items.size(),2u);EXPECT_DOUBLE_EQ(w.environmental_damage({8,8},20,false),35);
  EXPECT_DOUBLE_EQ(w.environmental_damage({0,0},20,false),80);
}
TEST(Events, RevisionRequiresKnownCurrentVersionAndAtomicCommit) {
  auto f=frame();Events events;events.merge(batch(f,Json::array({area("explosion",20,21)})),f);
  auto known=events.context(true);auto correction=known["events"][0];correction["rect"]={{1,1},{2,2}};
  auto unknown=correction;unknown["event_id"]="not-in-request";
  EXPECT_THROW(events.merge(batch(f,Json::array({correction,unknown})),f,&known),std::exception);
  EXPECT_EQ(events.context(true),known);
  auto other_kind=correction;other_kind["kind"]="illusion";
  EXPECT_THROW(events.merge(batch(f,Json::array({other_kind})),f,&known),std::exception);
  EXPECT_THROW(events.merge(batch(f,Json::array({correction,correction})),f,&known),std::exception);
  EXPECT_EQ(events.context(true),known);
  events.merge(batch(f,Json::array({correction})),f,&known);auto current=events.context(true);
  EXPECT_THROW(events.merge(batch(f,Json::array({correction})),f,&known),std::exception);
  EXPECT_EQ(events.context(true),current);
  auto absent=Json{{"events",Json::array()},{"effects",Json::array()}};
  EXPECT_THROW(events.merge(batch(f,Json::array({current["events"][0]})),f,&absent),std::exception);
}
TEST(Events, VersionedIdentityDoesNotGuessFromKindOrTick) {
  auto f=frame();Events events;events.merge(batch(f,Json::array({area("unrivaled",20,70)})),f);
  auto known=events.context(true);auto independent=area("unrivaled",20,70);independent["rect"]={{1,1},{3,3}};
  auto changes=events.merge(batch(f,Json::array({independent})),f,&known);
  EXPECT_EQ(events.items.size(),2u);ASSERT_EQ(changes.size(),1u);
  EXPECT_EQ(changes[0]["kind"],"event_identity_ambiguous");
  auto current=events.context(true);auto repeat=current["events"][0];
  EXPECT_TRUE(events.merge(batch(f,Json::array({repeat})),f,&current).empty());
  EXPECT_EQ(events.context(true),current);
  repeat.erase("event_id");repeat.erase("revision");repeat["end_tick"]=80;
  auto updates=events.merge(batch(f,Json::array({repeat})),f,&current);
  ASSERT_EQ(updates.size(),1u);EXPECT_EQ(updates[0]["identity_basis"],"exact_geometry");
  EXPECT_EQ(events.items[0].end,80);EXPECT_EQ(events.items[0].revision,1);
  auto revised=events.context(true);repeat["end_tick"]=90;
  EXPECT_THROW(events.merge(batch(f,Json::array({repeat})),f,&current),std::exception);
  EXPECT_EQ(events.context(true),revised);
  events.clear();events.merge(batch(f,Json::array({independent})),f);
  EXPECT_EQ(events.context(true)["events"][0]["event_id"],"event-0");
  EXPECT_EQ(events.context(true)["events"][0]["revision"],0);
}
TEST(Events, DelayedIllusionUsesActivationPositionAndExpires) {
  World w;auto f=frame(10);w.update(f);f.tick=11;f.pos={10,8};w.update(f);
  w.events.merge(batch(f,Json::array({area("illusion",10,11,{{"effect_ticks",5}})})),f);
  EXPECT_TRUE(w.effect("illusion","A",11));EXPECT_TRUE(w.effect("illusion","A",14));EXPECT_FALSE(w.effect("illusion","A",15));
  f.tick=12;f.alive=false;w.update(f);EXPECT_TRUE(w.effect("illusion","A",12));
}
TEST(Events, PanStartsNextCombatAndUsesCommonExpiry) {
  World w;auto f=frame(10);w.update(f);
  auto pan=area("pan",10,40,{{"movement_window_ticks",20},{"vulnerability_ticks",30},{"damage_multiplier",2}});
  w.events.merge(batch(f,Json::array({pan})),f);
  EXPECT_FALSE(w.effect("pan","A",10));
  f.tick=11;f.action_tick=10;f.moved=true;f.pos={9,8};w.update(f);
  EXPECT_TRUE(w.effect("pan","A",11));EXPECT_FALSE(w.effect("pan","A",40));
}
TEST(Events, AngelRequiresSuccessfulEntryAndPersistsAfterPointEnds) {
  World w;auto f=frame(10);w.update(f);
  Json angel{{"kind","angel"},{"start_tick",10},{"end_tick",12},{"points",{{9,8}}},{"params",{{"effect_ticks",10}}}};
  w.events.merge(batch(f,Json::array({angel})),f);
  f.tick=11;f.pos={9,8};f.action_tick=10;f.moved=false;w.update(f);EXPECT_FALSE(w.effect("angel","A",11));
  // Reconstruct another history with a confirmed successful move into the point.
  World entered;auto a=frame(10);entered.update(a);entered.events.merge(batch(a,Json::array({angel})),a);
  a.tick=11;a.action_tick=10;a.moved=true;a.pos={9,8};entered.update(a);
  EXPECT_TRUE(entered.effect("angel","A",15));EXPECT_FALSE(entered.effect("angel","A",20));
}
TEST(Events, FortressBuffStartsAfterRequiredResidenceAndSurvivesDeath) {
  World w;auto f=frame(10);w.update(f);
  w.events.merge(batch(f,Json::array({area("unrivaled",10,30,{{"stay_ticks",2}})})),f);
  f.tick=11;w.update(f);EXPECT_FALSE(w.effect("unrivaled","A",11));
  f.tick=12;w.update(f);EXPECT_TRUE(w.effect("unrivaled","A",12));
  f.tick=13;f.alive=false;w.update(f);EXPECT_TRUE(w.effect("unrivaled","A",13));EXPECT_FALSE(w.effect("unrivaled","A",30));
}
TEST(Events, TemporaryWallExpiresAndKnownOriginalObstacleRemains) {
  World w;auto f=frame(0);w.update(f);w.obstacles[{8,4}]={0,0};w.obstacles[{8,5}]={10,10};
  Json split{{"kind","world_split"},{"start_tick",10},{"end_tick",20},{"points",{{8,8}}}};
  w.events.merge(batch(f,Json::array({split})),f);
  EXPECT_TRUE(w.blocked({8,5},15));EXPECT_FALSE(w.blocked({8,5},20));EXPECT_TRUE(w.blocked({8,4},20));
}
TEST(Planner, EscapesExplosionBeforeActivation) {
  World w;auto f=frame(74);f.pos={4,4};f.entities.push_back({"resource_high","h","",{5,4},100,20,false,false});w.update(f);
  w.events.merge(batch(f,Json::array({area("explosion",75,76)})),f);
  Planner p(2,500);auto a=p.decide(w);ASSERT_EQ(a.type,"move");EXPECT_TRUE(a.direction=="up" || a.direction=="left");
  f.tick=75;f.hp=70;w.update(f);EXPECT_EQ(p.decide(w).type,"wait");
}
TEST(Planner, ReversesOnlyMoveCommandUnderIllusion) {
  World w;auto f=frame(0);f.entities.push_back({"resource_high","h","",{9,8},100,20,false,false});w.update(f);
  w.events.merge(batch(f,Json::array(),Json::array({{{"kind","illusion"},{"team","A"},{"start_tick",0},{"end_tick",10}}})),f);
  Planner p(1,100);auto a=p.decide(w);EXPECT_EQ(a.type,"move");EXPECT_EQ(a.direction,"left");
}
TEST(Planner, EventEscapePreservesTwoStepDeadlineInsteadOfTakingNearbyResource) {
  World w;auto f=frame(10);f.pos={5,7};f.hp=60;
  f.entities.push_back({"resource_high","h","",{6,7},100,20,false,false});w.update(f);
  auto event=area("explosion",12,13,{{"damage",80}});event["rect"]={{4,4},{10,10}};
  w.events.merge(batch(f,Json::array({event})),f);
  Planner old;auto greedy=old.decide(w);EXPECT_EQ(greedy.type,"move");EXPECT_EQ(greedy.direction,"right");
  PlannerConfig config;config.event_escape=true;Planner candidate(2,500,config);
  auto a=candidate.decide(w);ASSERT_EQ(a.type,"move");EXPECT_EQ(a.direction,"left");
  f.tick=11;f.pos={4,7};w.update(f);a=candidate.decide(w);
  ASSERT_EQ(a.type,"move");EXPECT_EQ(a.direction,"left");
  EXPECT_EQ(w.environmental_damage({3,7},12,false),0);
}
TEST(Planner, EventEscapeUsesSlackForResourcesAndHandlesReversedMovement) {
  World w;auto f=frame(10);f.pos={5,7};f.hp=60;
  f.entities.push_back({"resource_high","h","",{6,7},100,20,false,false});w.update(f);
  auto event=area("explosion",14,15,{{"damage",80}});event["rect"]={{4,4},{10,10}};
  w.events.merge(batch(f,Json::array({event})),f);
  PlannerConfig config;config.event_escape=true;Planner candidate(2,500,config);
  auto a=candidate.decide(w);ASSERT_EQ(a.type,"move");EXPECT_EQ(a.direction,"right");
  f.tick=11;f.pos={6,7};f.resource_value=20;f.entities.clear();w.update(f);
  w.events.merge(batch(f,Json::array(),Json::array({{{"kind","illusion"},{"team","A"},{"start_tick",11},{"end_tick",20}}})),f);
  for(int tick=11;tick<14;++tick) {
    f.tick=tick;if(tick>11)w.update(f);
    a=candidate.decide(w);ASSERT_EQ(a.type,"move");EXPECT_EQ(a.direction,"right"); // Actual motion is left.
    --f.pos.x;
  }
  EXPECT_EQ(f.pos,(Pos{3,7}));EXPECT_EQ(w.environmental_damage(f.pos,14,false),0);
}
TEST(Planner, EventEscapeDoesNotAbandonFiniteResourceForSurvivableFutureDamage) {
  World w;auto f=frame(10);f.pos={5,7};
  f.entities.push_back({"resource_high","h","",{6,7},100,20,false,false});w.update(f);
  auto event=area("explosion",12,13,{{"damage",80}});event["rect"]={{4,4},{10,10}};
  w.events.merge(batch(f,Json::array({event})),f);
  PlannerConfig config;config.event_escape=true;Planner candidate(2,500,config);
  auto a=candidate.decide(w);EXPECT_EQ(a.type,"move");EXPECT_EQ(a.direction,"right");
}
TEST(Planner, EventEscapeCombinesSuccessiveDamageBeforeTestingLethality) {
  World w;auto f=frame(10);f.pos={5,7};
  f.entities.push_back({"resource_high","h","",{6,7},100,20,false,false});w.update(f);
  auto first=area("explosion",11,12,{{"damage",50}});first["rect"]={{0,0},{23,23}};
  auto second=area("explosion",12,13,{{"damage",50}});second["rect"]={{4,4},{10,10}};
  w.events.merge(batch(f,Json::array({first,second})),f);
  PlannerConfig config;config.event_escape=true;Planner candidate(2,500,config);
  auto a=candidate.decide(w);ASSERT_EQ(a.type,"move");EXPECT_EQ(a.direction,"left");
  // The first blast cannot be avoided. Leave room for the second move so the
  // combined 100 damage does not kill us, even though each blast alone is safe.
}
TEST(Planner, UnavoidableSurvivableEventDoesNotDistortExplorationGoal) {
  World w;auto f=frame(10);w.update(f);
  Planner baseline;auto expected=baseline.decide(w);const auto target=baseline.target();
  auto event=area("explosion",11,12,{{"damage",80}});event["rect"]={{0,0},{23,23}};
  w.events.merge(batch(f,Json::array({event})),f);
  PlannerConfig config;config.event_escape=true;config.event_unavoidable_cost=true;Planner candidate(2,500,config);
  auto actual=candidate.decide(w);
  EXPECT_EQ(candidate.target(),target);EXPECT_EQ(actual.type,expected.type);EXPECT_EQ(actual.direction,expected.direction);
  EXPECT_EQ(w.environmental_damage(f.pos,11,false),80); // The real damage remains in world state.
}
TEST(Planner, FrontierWindowCanPreferFullNewFootprintOverNearbyThinStrip) {
  World w;auto f=frame(10);f.radius=1;w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  for(int y=7;y<=9;++y) {
    w.explored.erase({10,y});
    for(int x=16;x<=18;++x) w.explored.erase({x,y});
  }
  const auto observed=w.explored;
  Planner baseline;baseline.decide(w);ASSERT_TRUE(baseline.target());
  PlannerConfig config;config.frontier_distance_window=true;Planner candidate(2,500,config);
  candidate.decide(w);ASSERT_TRUE(candidate.target());
  EXPECT_LT(baseline.target()->x,16);EXPECT_GE(candidate.target()->x,16);
  EXPECT_EQ(candidate.reason,"explore");EXPECT_EQ(w.explored,observed);EXPECT_TRUE(w.resources.empty());
}
TEST(Planner, FrontierWindowPreservesKnownResourceAndStageAndLateBehavior) {
  PlannerConfig config;config.frontier_distance_window=true;
  World w;auto f=frame(10);f.entities.push_back({"resource_high","h","",{9,8},100,20,false,false});w.update(f);
  Planner baseline;Planner candidate(2,500,config);
  auto expected=baseline.decide(w),actual=candidate.decide(w);
  EXPECT_EQ(actual.type,expected.type);EXPECT_EQ(actual.direction,expected.direction);EXPECT_EQ(candidate.target(),baseline.target());
  EXPECT_EQ(candidate.reason,"resource");
  for(int stage:{1,2}) {
    World other;auto g=frame(stage==1?10:200);other.update(g);
    Planner control(stage,500);Planner test(stage,500,config);
    expected=control.decide(other);actual=test.decide(other);
    EXPECT_EQ(actual.type,expected.type);EXPECT_EQ(actual.direction,expected.direction);EXPECT_EQ(test.target(),control.target());
  }
}
TEST(Planner, EventEscapeRespectsWallsAndCurrentOccupiedExit) {
  World w;auto f=frame(10);f.pos={5,7};f.hp=60;
  f.entities.push_back({"obstacle","","",{4,7}});
  f.entities.push_back({"robot","B","B",{5,8},100,0,false,true});
  f.entities.push_back({"resource_high","h","",{6,7},100,20,false,false});w.update(f);
  auto event=area("explosion",13,14,{{"damage",80}});event["rect"]={{4,4},{10,10}};
  w.events.merge(batch(f,Json::array({event})),f);
  PlannerConfig config;config.event_escape=true;Planner candidate(2,500,config);
  auto a=candidate.decide(w);ASSERT_EQ(a.type,"move");EXPECT_EQ(a.direction,"up");
  // Up, left, left avoids the known wall; down is occupied this Tick.
}
TEST(Planner, EventEscapePreservesProtectedAndUnavoidableCases) {
  World w;auto f=frame(10);f.pos={5,7};f.hp=60;f.protected_spawn=true;w.update(f);
  auto event=area("explosion",12,13,{{"damage",80}});event["rect"]={{0,0},{23,23}};
  w.events.merge(batch(f,Json::array({event})),f);
  PlannerConfig config;config.event_escape=true;Planner candidate(2,500,config);
  EXPECT_NE(candidate.decide(w).type,"move");
  f.tick=11;f.protected_spawn=false;w.update(f);
  Planner old;auto baseline=old.decide(w),actual=candidate.decide(w);
  EXPECT_EQ(actual.type,baseline.type);EXPECT_EQ(actual.direction,baseline.direction);
}
TEST(Planner, InterpretationAblationWithholdsOnlyEventsAndPreservesValidatedContext) {
  World w;auto f=frame(10);f.pos={5,7};f.hp=60;
  f.entities.push_back({"resource_high","h","",{6,7},100,20,false,false});w.update(f);
  auto event=area("explosion",12,13,{{"damage",80}});event["rect"]={{4,4},{10,10}};
  w.events.merge(batch(f,Json::array({event}),Json::array({
    {{"kind","illusion"},{"team","A"},{"start_tick",10},{"end_tick",20}}})),f);
  const auto context=w.events.context();const auto observed=w.explored;
  PlannerConfig config;config.event_escape=true;
  Planner applied(2,500,config);auto escape=applied.decide(w);
  ASSERT_EQ(escape.type,"move");EXPECT_EQ(escape.direction,"right"); // Physical left under illusion.
  config.use_event_interpretation=false;Planner withheld(2,500,config);
  auto greedy=withheld.decide(w);EXPECT_EQ(greedy.type,"move");EXPECT_EQ(greedy.direction,"right");
  EXPECT_EQ(withheld.reason,"resource");EXPECT_EQ(withheld.target(),(std::optional<Pos>{{6,7}}));
  EXPECT_EQ(w.events.context(),context);EXPECT_EQ(w.explored,observed);EXPECT_EQ(w.history.size(),1u);
  World no_events=w;no_events.events.clear();Planner baseline;
  auto expected=baseline.decide(no_events);
  EXPECT_EQ(greedy.type,expected.type);EXPECT_EQ(greedy.direction,expected.direction);
}
TEST(Planner, ObstacleDoesNotBlockAttackAndProtectedNearestDoes) {
  World w;auto f=frame();f.entities.push_back({"obstacle","","",{9,8}});
  f.entities.push_back({"robot","B","B",{10,8},10,0,false,false});w.update(f);
  Planner p;auto a=p.decide(w);EXPECT_EQ(a.type,"attack");EXPECT_EQ(a.direction,"right");
  f.tick=1;f.entities.push_back({"robot","C","C",{9,8},100,0,false,true});w.update(f);
  a=p.decide(w);EXPECT_FALSE(a.type=="attack" && a.direction=="right");
}
TEST(Planner, DeathAndFirstStageNeverAttack) {
  World w;auto f=frame();f.alive=false;w.update(f);Planner p;EXPECT_EQ(p.decide(w).type,"wait");
  f.tick=1;f.alive=true;f.entities.push_back({"robot","B","B",{9,8},10,0,false,false});w.update(f);
  Planner stage1(1,100);EXPECT_TRUE(legal(stage1.decide(w),f,1));
}
TEST(Planner, PurityDefenseAvoidsLethalAttackAndReflection) {
  World w;auto f=frame(10);f.hp=15;f.entities.push_back({"robot","B","B",{9,8},100,0,true,false});w.update(f);
  w.events.merge(batch(f,Json::array({area("purity",10,30,{{"reflection_damage",20}})})),f);
  Planner p;EXPECT_EQ(p.decide(w).type,"defend");
}
TEST(Planner, PurityJointActionsNeverAddTheSameCarsAttackAndDefense) {
  for(double hp:{25.0,30.0,35.0}) {
    auto w=purity_trade_world(hp);Planner baseline;
    if(hp<=30) {EXPECT_EQ(baseline.decide(w).type,"defend");}
    PlannerConfig config;config.purity_joint_actions=true;Planner candidate(2,500,config);
    auto action=candidate.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"right");
    ASSERT_EQ(candidate.purity_evaluations.size(),1u);
    const auto &estimate=candidate.purity_evaluations.front();
    EXPECT_DOUBLE_EQ(estimate["expected_received"],20);EXPECT_DOUBLE_EQ(estimate["death_probability"],0);
    const auto &branches=estimate["branches_nondefend_then_defend"];
    EXPECT_DOUBLE_EQ(branches[0]["attack"],20);EXPECT_DOUBLE_EQ(branches[0]["kill_reward"],60);
    EXPECT_DOUBLE_EQ(branches[1]["attack"],0);EXPECT_DOUBLE_EQ(branches[1]["kill_reward"],0);
    for(const auto &b:branches) {EXPECT_DOUBLE_EQ(b["received"],20);EXPECT_FALSE(b["dies"].get<bool>());}
    EXPECT_EQ(w.current.hp,hp);EXPECT_EQ(w.current.effective_damage,80);EXPECT_EQ(w.history.size(),1u);
  }
}
TEST(Planner, PurityJointActionsKeepLethalBranchesAndPanDoesNotMultiplyReflection) {
  PlannerConfig config;config.purity_joint_actions=true;Planner candidate(2,500,config);
  for(double hp:{15.0,20.0}) {
    auto w=purity_trade_world(hp);EXPECT_EQ(candidate.decide(w).type,"defend");
    ASSERT_EQ(candidate.purity_evaluations.size(),1u);
    EXPECT_DOUBLE_EQ(candidate.purity_evaluations[0]["death_probability"],1);
    auto b=candidate.purity_evaluations[0]["branches_nondefend_then_defend"];
    EXPECT_DOUBLE_EQ(b[0]["adjusted_damage"],100);EXPECT_DOUBLE_EQ(b[1]["adjusted_damage"],20);
  }
  auto w=purity_trade_world(35);
  w.events.merge(batch(w.current,Json::array(),Json::array({
    {{"kind","pan"},{"team","A"},{"start_tick",200},{"end_tick",220},{"params",{{"damage_multiplier",2}}}}})),w.current);
  candidate.decide(w);const auto &estimate=candidate.purity_evaluations[0];
  EXPECT_NEAR(estimate["death_probability"].get<double>(),.35,1e-12);
  EXPECT_DOUBLE_EQ(estimate["expected_received"],27);
  EXPECT_DOUBLE_EQ(estimate["branches_nondefend_then_defend"][0]["received"],40);
  EXPECT_DOUBLE_EQ(estimate["branches_nondefend_then_defend"][1]["received"],20);
}
TEST(Planner, PurityJointActionsClipDamageAndApplyScoreCapInsideEachBranch) {
  auto w=purity_trade_world(25,10);w.current.effective_damage=490;
  PlannerConfig config;config.purity_joint_actions=true;Planner candidate(2,500,config);candidate.decide(w);
  const auto &b=candidate.purity_evaluations[0]["branches_nondefend_then_defend"];
  EXPECT_DOUBLE_EQ(b[0]["attack"],10);EXPECT_DOUBLE_EQ(b[0]["adjusted_damage"],560);
  EXPECT_DOUBLE_EQ(b[1]["adjusted_damage"],490);
  // Evaluate the score cap after each outcome, not after averaging D.
  EXPECT_NEAR(candidate.purity_evaluations[0]["score"].get<double>(),.348,1e-12);
  EXPECT_EQ(w.current.effective_damage,490);
}
TEST(Planner, PurityJointActionsIncludeEnvironmentAndUnrivaledDamage) {
  auto w=purity_trade_world(35);
  w.events.merge(batch(w.current,Json::array({area("explosion",200,201,{{"damage",20}})})),w.current);
  PlannerConfig config;config.purity_joint_actions=true;Planner candidate(2,500,config);candidate.decide(w);
  EXPECT_DOUBLE_EQ(candidate.purity_evaluations[0]["hp_after_environment"],15);
  EXPECT_DOUBLE_EQ(candidate.purity_evaluations[0]["death_probability"],1);
  w=purity_trade_world(35,40);
  w.events.merge(batch(w.current,Json::array(),Json::array({
    {{"kind","unrivaled"},{"team","A"},{"start_tick",200},{"end_tick",220},{"params",{{"attack_damage",50}}}},
    {{"kind","unrivaled"},{"team","B"},{"start_tick",200},{"end_tick",220},{"params",{{"attack_damage",50}}}}})),w.current);
  candidate.decide(w);ASSERT_EQ(candidate.purity_evaluations.size(),4u);
  for(const auto &estimate:candidate.purity_evaluations) {
    const auto &b=estimate["branches_nondefend_then_defend"];
    EXPECT_DOUBLE_EQ(b[0]["attack"],40);EXPECT_DOUBLE_EQ(b[0]["received"],50);
    EXPECT_DOUBLE_EQ(b[0]["kill_reward"],60);EXPECT_DOUBLE_EQ(b[1]["received"],20);
    EXPECT_NEAR(estimate["death_probability"].get<double>(),.35,1e-12);
  }
}
TEST(Planner, PurityJointActionsPreserveUncoveredContextsAndClearDiagnostics) {
  for(int scenario=0;scenario<11;++scenario) {
    auto w=purity_trade_world();PlannerConfig config;int stage=2;
    if(scenario==0) w.events.clear();
    if(scenario==1) w.current.protected_spawn=true;
    if(scenario==2) w.current.entities[0].protected_spawn=true;
    if(scenario==3) w.current.entities.push_back({"robot","C","C",{8,10},20,0,true,false});
    if(scenario==4) w.current.radius=3;
    if(scenario==5) w.current.tick=220;
    if(scenario==6) config.use_event_interpretation=false;
    if(scenario==7) config.score_aware_combat=false;
    if(scenario==8) config.tactical_forecast=true;
    if(scenario==9) stage=1;
    if(scenario==10) w.events.merge(batch(w.current,Json::array(),Json::array({
      {{"kind","unrivaled"},{"team","C"},{"start_tick",200},{"end_tick",220},{"params",{{"attack_radius",3}}}}})),w.current);
    Planner baseline(stage,500,config);config.purity_joint_actions=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current))<<scenario;
    EXPECT_TRUE(candidate.purity_evaluations.empty())<<scenario;
  }
  auto w=purity_trade_world();PlannerConfig config;config.purity_joint_actions=true;Planner candidate(2,500,config);
  candidate.decide(w);EXPECT_FALSE(candidate.purity_evaluations.empty());
  w.current.alive=false;candidate.decide(w);EXPECT_TRUE(candidate.purity_evaluations.empty());
  w.current.alive=true;candidate.decide(w);candidate.reset();EXPECT_TRUE(candidate.purity_evaluations.empty());
}
TEST(Planner, AvoidsPotentialPepperFireTriggeredByAdjacentEnemy) {
  World w;auto f=frame(10);f.entities.push_back({"resource_high","h","",{9,8},100,20,false,false});
  f.entities.push_back({"robot","B","B",{10,10},100,0,false,false});w.update(f);
  Json pepper{{"kind","pepper"},{"start_tick",10},{"end_tick",30},{"points",{{9,10}}},{"params",{{"damage",50}}}};
  w.events.merge(batch(f,Json::array({pepper})),f);
  Planner p;auto a=p.decide(w);EXPECT_FALSE(a.type=="move" && a.direction=="right");
}
TEST(Planner, CollectsFiniteResourcesBeforeDeferrableCoordinateTask) {
  World w;auto f=frame();
  f.entities.push_back({"resource_high","h","",{8,11},100,20,false,false});
  f.missions.push_back({{"id","reach"},{"type","reach"},{"position",{9,8}}});
  w.update(f);Planner p;auto a=p.decide(w);
  EXPECT_EQ(a.type,"move");EXPECT_EQ(a.direction,"down");
}
TEST(Planner, CoordinateTaskBecomesUrgentBeforeEnd) {
  World w;auto f=frame(490);
  f.entities.push_back({"resource_high","h","",{8,11},100,20,false,false});
  f.missions.push_back({{"id","reach"},{"type","reach"},{"position",{9,8}}});
  w.update(f);Planner p;auto a=p.decide(w);
  EXPECT_EQ(a.type,"move");EXPECT_EQ(a.direction,"right");
}
TEST(Planner, EmptyResourceMemoryDoesNotEndEarlyExploration) {
  World w;auto f=frame();f.entities.push_back({"robot","B","B",{8,12},100,0,false,false});
  w.update(f);Planner p;p.decide(w);EXPECT_EQ(p.reason,"explore");
}
TEST(Planner, TakesAvailableShotInsteadOfChasingFiringPositions) {
  World w;auto f=frame(200);f.entities.push_back({"robot","B","B",{8,10},100,0,false,false});
  w.update(f);Planner p;auto action=p.decide(w);
  EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"down");
}
TEST(Planner, ProductiveEscapeAdvancesEarlyExplorationInsteadOfTradingShots) {
  World w;auto f=frame(3);f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});w.update(f);
  PlannerConfig old_config;old_config.productive_escape=false;old_config.score_aware_combat=false;
  Planner baseline(2,500,old_config);
  EXPECT_EQ(baseline.decide(w).type,"attack");
  old_config.productive_escape=true;Planner candidate(2,500,old_config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"move");
  EXPECT_EQ(candidate.reason,"explore");
  EXPECT_TRUE(action.direction=="up" || action.direction=="down" || action.direction=="left");
}
TEST(Planner, ProductiveEscapePreservesImmediateFinisherAndLateDamage) {
  PlannerConfig config;config.productive_escape=true;
  for(int tick:{3,200}) {
    World w;auto f=frame(tick);
    f.entities.push_back({"robot","B","B",{10,8},tick==3?10.0:100.0,0,false,false});w.update(f);
    Planner p(2,500,config);auto action=p.decide(w);
    EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"right");
  }
}
TEST(Planner, ProductiveEscapeDoesNotPretendToEvadeCurrentLethalCombat) {
  World w;auto f=frame(3);f.hp=20;
  f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});w.update(f);
  PlannerConfig old_config;old_config.productive_escape=false;Planner baseline(2,500,old_config);auto old=baseline.decide(w);
  PlannerConfig config;config.productive_escape=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,old.type);EXPECT_EQ(action.direction,old.direction);
}
TEST(Planner, CanAttackWhileAcquiringFortressResidence) {
  World w;auto f=frame(10);f.entities.push_back({"robot","B","B",{9,8},100,0,true,false});
  w.update(f);w.events.merge(batch(f,Json::array({area("unrivaled",10,30,{{"stay_ticks",4}})})),f);
  Planner p;auto action=p.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"right");
}
TEST(Planner, TimedAngelWaitsAdjacentThenEntersOnActivation) {
  World w;auto f=frame(9);f.hp=20;w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  Json angel{{"kind","angel"},{"start_tick",10},{"end_tick",12},{"points",{{9,8}}},{"params",{{"effect_ticks",10}}}};
  w.events.merge(batch(f,Json::array({angel})),f);
  PlannerConfig config;config.event_timing=true;Planner p(2,500,config);
  auto waiting=p.decide(w);EXPECT_NE(waiting.type,"move");EXPECT_EQ(p.target(),std::optional<Pos>(f.pos));
  f.tick=10;f.action_tick=9;f.moved=false;w.update(f);
  auto enter=p.decide(w);ASSERT_EQ(enter.type,"move");EXPECT_EQ(enter.direction,"right");
  f.tick=11;f.action_tick=10;f.moved=true;f.pos={9,8};w.update(f);
  EXPECT_TRUE(w.effect("angel","A",11));
}
TEST(Planner, TimedAngelUsesLastActiveMovementTick) {
  World w;auto f=frame(11);f.hp=20;w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  Json angel{{"kind","angel"},{"start_tick",10},{"end_tick",12},{"points",{{9,8}}},{"params",{{"effect_ticks",10}}}};
  w.events.merge(batch(f,Json::array({angel})),f);
  PlannerConfig config;config.event_timing=true;Planner p(2,500,config);
  auto action=p.decide(w);EXPECT_EQ(p.target(),std::optional<Pos>(Pos{9,8}));
  ASSERT_EQ(action.type,"move");EXPECT_EQ(action.direction,"right");
  f.tick=12;f.pos={9,8};f.action_tick=11;f.moved=true;w.update(f);
  EXPECT_TRUE(w.effect("angel","A",12));
}
TEST(Planner, TimedAngelUnderfootLeavesAndReentersInsteadOfWaitingForGrant) {
  World w;auto f=frame(10);f.hp=20;w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  Json angel{{"kind","angel"},{"start_tick",10},{"end_tick",14},{"points",{{8,8}}}};
  w.events.merge(batch(f,Json::array({angel})),f);
  PlannerConfig config;config.event_timing=true;Planner p(2,500,config);
  auto leave=p.decide(w);ASSERT_EQ(leave.type,"move");
  if(leave.direction=="up") --f.pos.y;else if(leave.direction=="down") ++f.pos.y;
  else if(leave.direction=="left") --f.pos.x;else ++f.pos.x;
  f.tick=11;f.action_tick=10;f.moved=true;w.update(f);EXPECT_FALSE(w.effect("angel","A",11));
  auto enter=p.decide(w);EXPECT_EQ(enter.type,"move");EXPECT_EQ(p.target(),std::optional<Pos>(Pos{8,8}));
}
TEST(Planner, TimedAngelDoesNotAbandonResourceToRenewActiveHealing) {
  World w;auto f=frame(11);f.hp=20;
  f.entities.push_back({"resource_high","h","",{7,8},100,20,false,false});w.update(f);
  Json angel{{"kind","angel"},{"start_tick",10},{"end_tick",20},{"points",{{9,8}}}};
  Json effect{{"kind","angel"},{"team","A"},{"start_tick",10},{"end_tick",20}};
  w.events.merge(batch(f,Json::array({angel}),Json::array({effect})),f);
  PlannerConfig config;config.event_timing=true;Planner p(2,500,config);
  auto action=p.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"left");
}
TEST(Planner, TimedFortressRejectsBuffExpiringBeforeFirstUsableCombat) {
  World w;auto f=frame(8);
  f.entities.push_back({"resource_normal","n","",{9,8},100,10,false,false});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  w.events.merge(batch(f,Json::array({area("unrivaled",10,12,{{"stay_ticks",2}})})),f);
  PlannerConfig config;config.event_timing=true;Planner p(2,500,config);
  auto action=p.decide(w);EXPECT_EQ(p.target(),std::optional<Pos>(Pos{9,8}));
  EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"right");
}
TEST(Planner, FortressBudgetPrefersCollectionOverPrematureOrUnusableResidence) {
  for(int scenario=0;scenario<2;++scenario) {
    SCOPED_TRACE(scenario);
    World w;auto f=frame(scenario==0?10:8);
    f.entities.push_back({"resource_normal","n","",{10,8},100,10,false,false});w.update(f);
    for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
    w.events.merge(batch(f,Json::array({area("unrivaled",scenario==0?30:10,
      scenario==0?80:12,{{"stay_ticks",scenario==0?10:2}})})),f);
    const auto events=w.events.context();const auto resources=w.resources.size();
    Planner baseline;baseline.decide(w);
    EXPECT_EQ(baseline.target(),std::optional<Pos>(f.pos));
    EXPECT_EQ(baseline.goal_reason,"fortress");
    PlannerConfig config;config.fortress_time_budget=true;Planner candidate(2,500,config);
    auto action=candidate.decide(w);
    EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{10,8}));
    EXPECT_EQ(candidate.goal_reason,"resource");
    EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"right");
    EXPECT_EQ(w.events.context(),events);EXPECT_EQ(w.resources.size(),resources);EXPECT_EQ(w.current.pos,f.pos);
    f.tick++;f.alive=false;f.hp=0;w.update(f);candidate.decide(w);
    EXPECT_TRUE(candidate.goal_reason.empty());candidate.reset();EXPECT_TRUE(candidate.goal_reason.empty());
  }
}
TEST(Planner, FortressBudgetLeavesOtherTimingAndInterpretationBoundariesUnchanged) {
  for(int scenario=0;scenario<4;++scenario) {
    SCOPED_TRACE(scenario);
    World w;auto f=frame(9);f.hp=20;
    f.entities.push_back({"resource_normal","n","",{10,8},100,10,false,false});w.update(f);
    for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
    if(scenario==0 || scenario==3)
      w.events.merge(batch(f,Json::array({area("unrivaled",30,80,{{"stay_ticks",10}})})),f);
    if(scenario==2) {
      Json angel{{"kind","angel"},{"start_tick",10},{"end_tick",12},{"points",{{9,8}}}};
      w.events.merge(batch(f,Json::array({angel})),f);
    }
    PlannerConfig config;config.use_event_interpretation=scenario!=3;
    int stage=scenario==0?1:2;Planner baseline(stage,500,config);
    config.fortress_time_budget=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(f),candidate.decide(w).json(f));
    EXPECT_EQ(baseline.target(),candidate.target());EXPECT_EQ(baseline.goal_reason,candidate.goal_reason);
  }
}
TEST(Planner, CollectsResourceAlreadyUnderRobotWithoutWalkingAway) {
  World w;auto f=frame();f.entities.push_back({"resource_high","underfoot","",f.pos,100,20,false,false});
  w.update(f);Planner p;EXPECT_NE(p.decide(w).type,"move");EXPECT_EQ(p.target(),std::optional<Pos>(f.pos));
}
TEST(Planner, ResourceRolloutAccountsForObservedWallDetour) {
  World w;auto f=frame();f.width=f.height=9;f.pos={4,4};
  for(int y=0;y<8;++y) f.entities.push_back({"obstacle","","",{5,y}});
  f.entities.push_back({"resource_normal","near","",{4,5},100,10,false,false});
  f.entities.push_back({"resource_high","across-wall","",{6,5},100,20,false,false});
  f.entities.push_back({"resource_high","open-side","",{2,4},100,20,false,false});
  w.update(f);PlannerConfig config;config.resource_paths_exact=true;Planner p(2,500,config);
  auto action=p.decide(w);EXPECT_EQ(p.target(),std::optional<Pos>(Pos{2,4}));
  EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"left");
}
TEST(Planner, DefaultTransientOccupancyPlansBeyondRobotButNeverEntersOccupiedNextCell) {
  World w;auto f=frame(10);f.width=5;f.height=7;f.pos={2,1};
  f.missions.push_back({{"id","high"},{"type","high_resources"},{"target",1}});
  f.entities.push_back({"robot","B","B",{2,3},100,0,false,true});
  f.entities.push_back({"resource_high","h","",{2,5},100,20,false,false});w.update(f);
  // A known single-cell corridor. The protected robot is two steps away,
  // leaving a legal approach step while its future occupancy is uncertain.
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) {
    w.explored.insert({x,y});if(x!=2) w.obstacles[{x,y}]={0,10};
  }
  PlannerConfig config;Planner candidate;
  auto approach=candidate.decide(w);
  EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{2,5}));
  EXPECT_EQ(approach.type,"move");EXPECT_EQ(approach.direction,"down");
  f.tick=11;f.pos={2,2};w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) if(x!=2) w.obstacles[{x,y}]={0,11};
  auto blocked=candidate.decide(w);
  EXPECT_FALSE(blocked.type=="move" && blocked.direction=="down");
  Planner stage1(1,100,config);stage1.decide(w);
  EXPECT_NE(stage1.target(),std::optional<Pos>(Pos{2,5}));
}
TEST(Planner, RejectsInvalidExperimentalParameters) {
  PlannerConfig config;config.early_movement_scale=0;EXPECT_THROW(Planner(2,500,config),std::exception);
  config.early_movement_scale=1;config.resource_lookahead=4;EXPECT_THROW(Planner(2,500,config),std::exception);
}
TEST(Planner, ForecastRecognizesProtectedRobotBlockingIncomingNormalShot) {
  World w;auto f=frame(10);f.hp=15;
  f.entities.push_back({"robot","C","C",{9,8},100,0,false,true});
  f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});
  f.entities.push_back({"resource_high","h","",{8,9},100,20,false,false});
  w.update(f);PlannerConfig config;config.tactical_forecast=true;Planner p(2,500,config);
  auto action=p.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");
}
TEST(Planner, VisibleOcclusionAvoidsDefenseWithoutEnablingOpponentForecast) {
  for(bool protected_blocker:{false,true}) {
    World w;auto f=frame(10);f.hp=protected_blocker?15:30;
    f.entities.push_back({"robot","C","C",{9,8},100,0,false,protected_blocker});
    f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});
    f.entities.push_back({"resource_high","h","",{8,9},100,20,false,false});w.update(f);
    Planner baseline;EXPECT_EQ(baseline.decide(w).type,"defend");
    PlannerConfig config;config.ray_occlusion=true;ASSERT_FALSE(config.tactical_forecast);
    Planner candidate(2,500,config);auto a=candidate.decide(w);
    EXPECT_EQ(a.type,"move");EXPECT_EQ(a.direction,"down");
    EXPECT_EQ(candidate.target(),(std::optional<Pos>{{8,9}}));
  }
}
TEST(Planner, OcclusionKeepsObstacleTransparentAndUnrivaledUnblocked) {
  PlannerConfig config;config.ray_occlusion=true;
  World w;auto f=frame(10);f.hp=15;
  f.entities.push_back({"obstacle","","",{9,8}});
  f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});
  f.entities.push_back({"resource_high","h","",{8,9},100,20,false,false});w.update(f);
  Planner candidate(2,500,config);EXPECT_EQ(candidate.decide(w).type,"defend");
  f.tick=11;f.hp=30;f.entities.front()={"robot","C","C",{9,8},100,0,false,true};w.update(f);
  w.events.merge(batch(f,Json::array(),Json::array({{
    {"kind","unrivaled"},{"team","B"},{"start_tick",11},{"end_tick",20},
    {"params",{{"attack_radius",2},{"attack_damage",50},{"defended_damage",10}}}}})),f);
  EXPECT_EQ(candidate.decide(w).type,"defend");
}
TEST(Planner, ForecastWaitsForObservedMovingEnemyToEnterFiringLine) {
  World w;auto f=frame(199);f.pos={8,9};
  f.entities.push_back({"robot","B","B",{10,7},100,0,false,false});w.update(f);
  f.tick=200;f.entities.front().pos={10,8};w.update(f);
  // No unexplored region remains to compete with this immediate opportunity.
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  PlannerConfig config;config.tactical_forecast=true;Planner p(2,500,config);
  EXPECT_NE(p.decide(w).type,"move");
  f.tick=201;f.entities.front().pos={10,9};w.update(f);
  auto action=p.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"right");
}
TEST(Planner, LateForecastPreservesResourcePhaseAndActivatesAtConfiguredFraction) {
  World w;auto f=frame(10);f.hp=15;
  f.entities.push_back({"robot","C","C",{9,8},100,0,false,true});
  f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});
  f.entities.push_back({"resource_high","h","",{8,9},100,20,false,false});w.update(f);
  Planner baseline;PlannerConfig config;config.tactical_forecast=true;config.forecast_start_tick=-1;
  Planner candidate(2,500,config);
  auto ordinary=baseline.decide(w),early=candidate.decide(w);
  EXPECT_EQ(early.type,ordinary.type);EXPECT_EQ(early.direction,ordinary.direction);
  EXPECT_EQ(candidate.target(),baseline.target());
  // The shielded intermediate robot blocks the incoming normal shot. The
  // late forecast recognizes that staying to defend is unnecessary.
  f.tick=100;w.update(f);auto late=candidate.decide(w);
  EXPECT_EQ(late.type,"move");EXPECT_EQ(late.direction,"down");
  config.forecast_start_tick=501;EXPECT_THROW(Planner(2,500,config),std::exception);
  config.forecast_start_tick=-2;EXPECT_THROW(Planner(2,500,config),std::exception);
}
TEST(Planner, ForecastDoesNotChaseUnreachableEarlyCompetitor) {
  World w;auto f=frame(9);
  f.entities.push_back({"robot","B","B",{10,7},100,0,false,false});
  f.entities.push_back({"resource_high","h","",{8,12},100,20,false,false});w.update(f);
  f.tick=10;f.entities.front().pos={10,6};w.update(f);w.recent_competition["B"]=10;
  PlannerConfig config;config.tactical_forecast=true;config.early_interception=true;Planner p(2,500,config);
  auto action=p.decide(w);EXPECT_EQ(p.reason,"resource");
  EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");
}
TEST(Planner, DefaultCompetitionCueKeepsExploringAndCanDisengage) {
  World w;auto f=frame(9);f.entities.push_back({"resource_normal","n","",{8,11},100,10,false,false});w.update(f);
  f.tick=10;f.entities.clear();f.entities.push_back({"robot","B","B",{8,11},100,0,false,false});w.update(f);
  ASSERT_EQ(w.recent_competition.at("B"),10);
  Planner current;EXPECT_EQ(current.decide(w).type,"move");EXPECT_EQ(current.reason,"explore");
  PlannerConfig previous;previous.early_interception=true;Planner opt_in(2,500,previous);
  opt_in.decide(w);EXPECT_EQ(opt_in.reason,"engage");
  f.tick=11;f.entities.front().pos={8,10};w.update(f);auto shot=current.decide(w);
  EXPECT_EQ(shot.type,"move");EXPECT_EQ(shot.direction,"right");EXPECT_EQ(current.reason,"explore");
}
TEST(Planner, DisablingEarlyInterceptionPreservesKnownResourcesAndAvailableShots) {
  World w;auto f=frame(10);
  f.entities.push_back({"robot","B","B",{10,6},100,0,false,false});
  f.entities.push_back({"resource_high","h","",{8,12},100,20,false,false});w.update(f);
  w.recent_competition["B"]=10;
  PlannerConfig config;config.early_interception=false;config.frontier_lookahead=true;
  config.productive_escape=false;config.score_aware_combat=false;Planner p(2,500,config);
  auto action=p.decide(w);EXPECT_EQ(p.reason,"resource");EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");
  f.tick=11;f.entities.front().pos={8,10};f.entities.erase(f.entities.begin()+1);w.update(f);
  action=p.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"down");
}
TEST(Planner, ExplorationTourStaysWithinOwnMemoryAndDoesNotAffectFirstStage) {
  World w;auto f=frame();w.update(f);
  auto explored=w.explored;auto history=w.history.size();
  PlannerConfig config;config.frontier_lookahead=true;Planner tour(2,500,config);
  auto action=tour.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(tour.reason,"explore");
  ASSERT_TRUE(tour.target());
  // A visible destination can still reveal new cells around its footprint.
  bool reveals_unknown=false;Pos target=*tour.target();
  for(int x=target.x-f.radius;x<=target.x+f.radius;++x) for(int y=target.y-f.radius;y<=target.y+f.radius;++y)
    if(w.inside({x,y}) && !w.explored.count({x,y})) reveals_unknown=true;
  EXPECT_TRUE(reveals_unknown);
  EXPECT_EQ(w.explored,explored);EXPECT_EQ(w.history.size(),history);EXPECT_TRUE(w.resources.empty());
  Planner first(1,100),first_tour(1,100,config);
  EXPECT_EQ(first.decide(w).json(f),first_tour.decide(w).json(f));EXPECT_EQ(first.target(),first_tour.target());
}
TEST(Planner, EqualResourcePathsPreferFirstStepThatRevealsUnknownCells) {
  for(bool transpose:{false,true}) {
    World w;auto f=frame();f.entities.push_back({"resource_high","h","",{10,10},100,20,false,false});w.update(f);
    w.explored.clear();
    for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y)
      if(!(transpose?(x==13 && y>=4 && y<=12):(y==13 && x>=4 && x<=12))) w.explored.insert({x,y});
    PlannerConfig config;config.information_paths=true;Planner p(2,500,config);
    auto action=p.decide(w);EXPECT_EQ(p.target(),std::optional<Pos>(Pos{10,10}));
    EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,transpose?"right":"down");
  }
}
TEST(Planner, KnownWallDetourChangesResourceRaceEstimateWithoutEnemyTruth) {
  World w;auto f=frame(10);
  f.entities.push_back({"resource_high","h","",{6,11},100,20,false,false});
  f.entities.push_back({"resource_normal","n","",{8,4},100,10,false,false});
  f.entities.push_back({"robot","B","B",{4,11},100,0,false,false});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  for(int y=7;y<=17;++y) w.obstacles[{5,y}]={0,0};
  PlannerConfig config;config.race_aware_resources=true;Planner p(2,500,config);
  auto action=p.decide(w);EXPECT_EQ(p.target(),std::optional<Pos>(Pos{6,11}));
  EXPECT_EQ(action.type,"move");EXPECT_TRUE(action.direction=="left" || action.direction=="down");
  Planner baseline;baseline.decide(w);EXPECT_EQ(baseline.target(),std::optional<Pos>(Pos{8,4}));
}
TEST(Planner, ResourceRaceAndInformationPathsDoNotChangeFirstStage) {
  World w;auto f=frame();f.entities.push_back({"resource_high","h","",{10,10},100,20,false,false});w.update(f);
  PlannerConfig config;config.race_aware_resources=true;config.information_paths=true;
  config.resource_beam=true;
  Planner ordinary(1,100),experimental(1,100,config);
  EXPECT_EQ(ordinary.decide(w).json(f),experimental.decide(w).json(f));EXPECT_EQ(ordinary.target(),experimental.target());
}
TEST(Planner, ContestedPublicTaskResourceUsesOwnLeadWithoutClaimingOpponentProgress) {
  World w;auto f=frame(10);
  f.missions.push_back({{"id","high"},{"type","high_resources"},{"target",1}});
  f.completed.insert("high");f.resource_value=20;
  f.entities.push_back({"resource_high","safe","",{6,8},100,20,false,false});
  f.entities.push_back({"resource_high","contested","",{10,9},100,20,false,false});
  f.entities.push_back({"robot","B","B",{12,11},100,0,false,true});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  auto resources=w.resources;auto enemies=w.enemies;auto completed=w.current.completed;
  PlannerConfig config;config.resource_lookahead=1;
  Planner baseline(2,500,config);baseline.decide(w);
  EXPECT_EQ(baseline.target(),std::optional<Pos>(Pos{6,8}));
  config.contested_task_resources=true;Planner candidate(2,500,config);
  EXPECT_EQ(candidate.decide(w).type,"move");
  EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{10,9}));
  EXPECT_EQ(w.resources.size(),resources.size());EXPECT_EQ(w.enemies.size(),enemies.size());
  EXPECT_EQ(w.current.completed,completed);EXPECT_EQ(w.current.resource_value,20);
  EXPECT_EQ(w.high_collected,0); // No opponent or own collection was invented.
}
TEST(Planner, PursuitPauseBreaksRepeatedDiagonalOrbitUsingOwnObservationsOnly) {
  for(bool reflect:{false,true}) {
    World w;std::array<Pos,5> self{{{8,8},{9,8},{9,9},{8,9},{8,8}}};
    std::array<Pos,5> rival{{{9,9},{8,9},{8,8},{9,8},{9,9}}};
    for(int i=0;i<5;++i) {
      auto f=frame(120+i);f.pos=self[i];if(reflect) f.pos.y=20-f.pos.y;
      f.hp=80;f.effective_damage=50;Pos enemy=rival[i];if(reflect) enemy.y=20-enemy.y;
      f.entities.push_back({"robot","B","B",enemy,60,0,false,false});w.update(f);
    }
    for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
    PlannerConfig previous;previous.break_pursuit_cycles=false;
    Planner baseline(2,500,previous);EXPECT_EQ(baseline.decide(w).type,"move");
    auto history=w.history.size();auto explored=w.explored;
    PlannerConfig config;config.break_pursuit_cycles=true;Planner candidate(2,500,config);
    EXPECT_EQ(candidate.decide(w).type,"wait");
    EXPECT_EQ(w.history.size(),history);EXPECT_EQ(w.explored,explored);
    // The pause itself prevents another complete moving orbit next Tick.
    auto f=w.current;++f.tick;w.update(f);
    Planner ordinary(2,500,previous),continued(2,500,config);
    EXPECT_EQ(ordinary.decide(w).json(f),continued.decide(w).json(f));
  }
}
TEST(Planner, PursuitPausePreservesProgressResourceShotsAndStage) {
  for(int scenario=0;scenario<8;++scenario) {
    World w;std::array<Pos,5> self{{{8,8},{9,8},{9,9},{8,9},{8,8}}};
    std::array<Pos,5> rival{{{9,9},{8,9},{8,8},{9,8},{9,9}}};
    for(int i=0;i<5;++i) {
      auto f=frame((scenario==0?10:(scenario==5?495:120))+i);f.pos=self[i];
      f.hp=scenario==6?40:80;f.effective_damage=scenario==7?500:50;
      if(scenario==1 && i==0) f.effective_damage=30;
      f.entities.push_back({"robot","B","B",rival[i],60,0,false,false});
      if(scenario==2) f.entities.push_back({"resource_high","h","",{7,8},100,20,false,false});
      if(scenario==3) f.entities.push_back({"robot","C","C",{8,10},60,0,false,false});
      w.update(f);
    }
    for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
    int stage=scenario==4?1:2;PlannerConfig previous;previous.break_pursuit_cycles=false;
    Planner baseline(stage,500,previous);
    PlannerConfig config;config.break_pursuit_cycles=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current))<<scenario;
  }
}
TEST(Planner, LateCoordinateTaskCanDeferOptionalHuntWithoutInventingProgress) {
  World w;auto f=frame(200);f.effective_damage=620;
  f.missions.push_back({{"id","destination"},{"type","reach"},{"position",{20,8}}});
  f.entities.push_back({"robot","B","B",{9,9},100,0,false,false});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  Planner baseline;baseline.decide(w);
  EXPECT_NE(baseline.target(),std::optional<Pos>(Pos{20,8}));
  auto explored=w.explored;auto history=w.history.size();
  PlannerConfig config;config.coordinate_before_hunt=true;Planner candidate(2,500,config);
  EXPECT_EQ(candidate.decide(w).type,"move");
  EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{20,8}));
  EXPECT_EQ(w.explored,explored);EXPECT_EQ(w.history.size(),history);
  EXPECT_TRUE(w.current.completed.empty());EXPECT_EQ(w.current.effective_damage,620);
  // A present shot is still evaluated even when the route target is a task.
  f.tick=201;f.missions[0]["position"]={8,8};
  f.entities.front().pos={8,10};f.entities.front().hp=20;w.update(f);
  EXPECT_EQ(candidate.decide(w).type,"attack");
}
TEST(Planner, CoordinateBeforeHuntPreservesEarlyUnknownResourceAndCompletedCases) {
  for(int scenario=0;scenario<8;++scenario) {
    World w;auto f=frame(scenario==0?100:(scenario==6?495:200));
    f.effective_damage=scenario==7?600:620;
    f.missions.push_back({{"id","destination"},{"type","reach"},{"position",{20,8}}});
    f.entities.push_back({"robot","B","B",{9,9},100,0,false,false});
    if(scenario==2) f.entities.push_back({"resource_normal","n","",{7,8},100,10,false,false});
    if(scenario==3) f.completed.insert("destination");
    if(scenario==4) f.missions[0]["position"]={30,8};
    w.update(f);
    if(scenario!=1) for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
    int stage=scenario==5?1:2;Planner baseline(stage,500);
    PlannerConfig config;config.coordinate_before_hunt=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current))<<scenario;
  }
}
TEST(Planner, ObservedDefenseStandoffRetainsRiskButAllowsPositiveFinisherTrade) {
  World w;
  for(int tick=120;tick<124;++tick) {
    auto f=frame(tick);f.hp=20;f.effective_damage=80;
    f.entities.push_back({"robot","B","B",{10,8},20,0,true,false});w.update(f);
  }
  for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
  Planner baseline;EXPECT_EQ(baseline.decide(w).type,"defend");
  auto damage=w.current.effective_damage;auto history=w.history.size();
  PlannerConfig config;config.standoff_fire_risk=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"right");
  EXPECT_EQ(w.current.effective_damage,damage);EXPECT_EQ(w.history.size(),history);
}
TEST(Planner, StandoffExpectedIncomingStillPricesTheLethalBranch) {
  World w;
  for(int tick=120;tick<124;++tick) {
    auto f=frame(tick);f.hp=20;f.effective_damage=80;
    f.entities.push_back({"robot","B","B",{10,8},100,0,true,false});w.update(f);
  }
  for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
  PlannerConfig config;config.standoff_fire_risk=true;Planner candidate(2,500,config);
  // Mean incoming is survivable, but a real shot can kill us. Without the
  // probabilistic death loss this low-value non-finisher would be preferred.
  EXPECT_EQ(candidate.decide(w).type,"defend");
}
TEST(Planner, StandoffInferenceNeedsContinuousStableDefenseAndIncompleteCombatScore) {
  for(int scenario=0;scenario<8;++scenario) {
    World w;
    for(int i=0;i<4;++i) {
      if(scenario==0 && i==1) continue;
      auto f=frame((scenario==1?10:120)+i);f.hp=20;f.effective_damage=scenario==2?500:80;
      if(scenario==3 && i==0) f.effective_damage=60;
      if(scenario==4 && i==0) f.hp=30;
      f.entities.push_back({"robot","B","B",{10,8},20,0,scenario!=5 || i!=1,false});w.update(f);
    }
    for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
    if(scenario==6) w.events.merge(batch(w.current,Json::array({area("purity",120,150)})),w.current);
    int stage=scenario==7?1:2;
    Planner baseline(stage,500);PlannerConfig config;config.standoff_fire_risk=true;
    Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current))<<scenario;
  }
}
TEST(Planner, AttackOnlyProbeNeedsLongLowHpStandoffAndLeavesOwnTruthUntouched) {
  World w;
  for(int tick=200;tick<208;++tick) {
    auto f=frame(tick);f.hp=20;f.effective_damage=80;
    f.entities.push_back({"robot","B","B",{10,8},20,0,true,false});w.update(f);
  }
  for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
  PlannerConfig config;config.standoff_robust_trade=false;Planner baseline(2,500,config);
  EXPECT_EQ(baseline.decide(w).type,"defend");
  auto history=w.history.size();auto explored=w.explored;
  config.standoff_attack_probe=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"right");
  EXPECT_EQ(w.current.hp,20);EXPECT_EQ(w.current.effective_damage,80);
  EXPECT_TRUE(w.current.completed.empty());EXPECT_EQ(w.history.size(),history);EXPECT_EQ(w.explored,explored);
  // A newly wounded opponent resets the continuous evidence; no uncontrolled
  // repeated probing is inferred from the previous, different HP state.
  auto f=w.current;++f.tick;f.entities.front().hp=10;w.update(f);
  config.standoff_attack_probe=false;Planner ordinary(2,500,config);
  EXPECT_EQ(ordinary.decide(w).json(f),candidate.decide(w).json(f));
}
TEST(Planner, AttackOnlyProbePreservesInsufficientEvidenceHighHpAndOtherThreats) {
  for(int scenario=0;scenario<12;++scenario) {
    World w;
    for(int i=0;i<8;++i) {
      if(scenario==0 && i==2) continue;
      auto f=frame((scenario==1?100:(scenario==2?490:200))+i);
      f.hp=scenario==3?40:20;f.effective_damage=scenario==4?500:80;
      if(scenario==5 && i==0) f.resource_value=10;
      f.entities.push_back({"robot","B","B",{10,8},scenario==6?100.0:20.0,0,scenario!=7 || i!=2,false});
      if(scenario==8) f.entities.push_back({"robot","C","C",{8,10},20,0,true,false});
      if(scenario==9) f.protected_spawn=true;
      w.update(f);
    }
    for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
    if(scenario==10) w.events.merge(batch(w.current,Json::array({area("purity",200,250)})),w.current);
    int stage=scenario==11?1:2;PlannerConfig config;config.standoff_robust_trade=false;
    Planner baseline(stage,500,config);
    config.standoff_attack_probe=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current))<<scenario;
  }
}
TEST(Planner, RobustStandoffTradeSeparatesEnemyFireDefenseAndSimultaneousDeaths) {
  for(double enemy_hp:{20.0,10.0}) {
    World w;
    for(int tick=200;tick<208;++tick) {
      auto f=frame(tick);f.hp=20;f.effective_damage=80;
      f.entities.push_back({"robot","B","B",{10,8},enemy_hp,0,true,false});w.update(f);
    }
    for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
    PlannerConfig config;config.standoff_robust_trade=false;Planner baseline(2,500,config);
    EXPECT_EQ(baseline.decide(w).type,"defend");
    config.standoff_robust_trade=true;Planner candidate(2,500,config);
    auto action=candidate.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"right");
    EXPECT_EQ(w.current.hp,20);EXPECT_EQ(w.current.effective_damage,80);EXPECT_TRUE(w.current.completed.empty());
  }
}
TEST(Planner, RobustStandoffTradeRequiresContributorVisibilityAndPreservesImmediateTask) {
  for(int scenario=0;scenario<6;++scenario) {
    World w;
    for(int tick=200;tick<208;++tick) {
      auto f=frame(tick);f.hp=20;f.effective_damage=scenario==2?500:80;
      if(scenario==0) f.radius=3; // Another contributor may be outside our view.
      if(scenario==1) f.missions.push_back({{"id","here"},{"type","reach"},{"position",{8,8}}});
      f.entities.push_back({"robot","B","B",{10,8},20,0,true,false});
      if(scenario==3) f.entities.push_back({"robot","C","C",{8,10},20,0,true,false});
      if(scenario==4 && tick==203) continue;
      w.update(f);
    }
    for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
    int stage=scenario==5?1:2;PlannerConfig config;config.standoff_robust_trade=false;
    Planner baseline(stage,500,config);config.standoff_robust_trade=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current))<<scenario;
  }
}
TEST(Planner, RobustGuaranteedFinisherDoesNotWaitForAnotherStandoffIncludingLastTick) {
  for(int tick:{200,499}) {
    World w;auto f=frame(tick);f.hp=20;f.effective_damage=80;
    f.entities.push_back({"robot","B","B",{10,8},10,0,true,false});w.update(f);
    for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
    PlannerConfig config;config.standoff_robust_trade=false;Planner baseline(2,500,config);
    EXPECT_EQ(baseline.decide(w).type,"defend");
    config.standoff_robust_trade=true;Planner candidate(2,500,config);
    auto action=candidate.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"right");
    EXPECT_EQ(w.current.effective_damage,80);EXPECT_EQ(w.history.size(),1u);
  }
}
TEST(Planner, OpeningInformationKeepsTargetCostAndStepsAndOnlyChangesAnEqualRoute) {
  auto w=opening_route_world();auto explored=w.explored;auto resources=w.resources.size();
  Planner baseline;auto original=baseline.decide(w);ASSERT_EQ(original.type,"move");ASSERT_EQ(original.direction,"right");
  PlannerConfig config;config.opening_route_information=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");
  EXPECT_EQ(candidate.target(),baseline.target());EXPECT_EQ(candidate.goal_reason,"resource");
  const auto &e=candidate.opening_route_evaluation;ASSERT_FALSE(e.is_null());
  EXPECT_DOUBLE_EQ(e["cost"],4);EXPECT_EQ(e["steps"],4);EXPECT_EQ(e["equal_first_mask"],10);
  EXPECT_EQ(e["baseline_new_cells"],0);EXPECT_EQ(e["proposed_new_cells"],9);
  EXPECT_EQ(w.explored,explored);EXPECT_EQ(w.resources.size(),resources);EXPECT_EQ(w.history.size(),1u);
  EXPECT_EQ(w.high_collected,0);EXPECT_EQ(w.current.resource_value,0);
}
TEST(Planner, OpeningInformationDoesNotDetourThroughBlockedOrOccupiedFirstSteps) {
  for(bool occupied:{false,true}) {
    auto w=opening_route_world();
    if(occupied) w.current.entities.push_back({"robot","B","B",{8,9},100,0,false,true});
    else w.obstacles[{8,9}]={0,5};
    Planner baseline;PlannerConfig config;config.opening_route_information=true;Planner candidate(2,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current));
    EXPECT_TRUE(candidate.opening_route_evaluation.is_null());
  }
  auto w=opening_route_world();w.explored.erase({13,12}); // Baseline already reveals a new cell.
  Planner baseline;PlannerConfig config;config.opening_route_information=true;Planner candidate(2,500,config);
  EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current));
  EXPECT_TRUE(candidate.opening_route_evaluation.is_null());
}
TEST(Planner, OpeningInformationPreservesHighValueKnownEventsAndOtherScopes) {
  for(int scenario=0;scenario<10;++scenario) {
    auto w=opening_route_world();PlannerConfig config;int stage=2;
    if(scenario==0) w.current.tick=60;
    if(scenario==1) w.high_collected=1;
    if(scenario==2) w.resources["high"]={{5,5},20,5};
    if(scenario==3) {
      w.current.missions.push_back({{"id","high"},{"type","high_resources"},{"target",1}});
      w.current.completed.insert("high");
    }
    if(scenario==4) w.events.merge(batch(w.current,Json::array({area("purity",30,50)})),w.current);
    if(scenario==5) w.events.merge(batch(w.current,Json::array(),Json::array({
      {{"kind","angel"},{"team","A"},{"start_tick",0},{"end_tick",10}}})),w.current);
    if(scenario==6) config.information_paths=true;
    if(scenario==7) config.resource_beam=true;
    if(scenario==8) stage=1;
    if(scenario==9) w.resources.clear();
    Planner baseline(stage,500,config);config.opening_route_information=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current))<<scenario;
    EXPECT_TRUE(candidate.opening_route_evaluation.is_null())<<scenario;
  }
}
TEST(Planner, OpeningInformationStillAllowsImmediateFinisherAndClearsItsEstimate) {
  auto w=opening_route_world();w.current.entities.push_back({"robot","B","B",{8,6},10,0,false,false});
  Planner baseline;PlannerConfig config;config.opening_route_information=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"up");
  EXPECT_EQ(action.json(w.current),baseline.decide(w).json(w.current));
  EXPECT_FALSE(candidate.opening_route_evaluation.is_null());
  w.current.alive=false;candidate.decide(w);EXPECT_TRUE(candidate.opening_route_evaluation.is_null());
  w.current.alive=true;candidate.decide(w);candidate.reset();EXPECT_TRUE(candidate.opening_route_evaluation.is_null());
}
TEST(Planner, ResourceRouteCollectsObservedPickupWithoutDelayingOriginalGoal) {
  auto w=resource_pickup_world();auto explored=w.explored;auto resources=w.resources.size();
  Planner baseline;auto old=baseline.decide(w);ASSERT_EQ(old.type,"move");ASSERT_EQ(old.direction,"right");
  PlannerConfig config;config.resource_route_pickups=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");
  EXPECT_EQ(candidate.target(),baseline.target());EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{10,10}));
  const auto &e=candidate.resource_route_evaluation;ASSERT_FALSE(e.is_null());
  EXPECT_EQ(e["pickup"],Json({8,10}));EXPECT_EQ(e["pickup_value"],10);
  EXPECT_EQ(e["steps"],4);EXPECT_EQ(e["via_steps"],4);EXPECT_EQ(e["pickup_arrival_steps"],2);
  EXPECT_DOUBLE_EQ(e["cost"],4);EXPECT_EQ(e["cost"],e["via_cost"]);
  EXPECT_EQ(w.explored,explored);EXPECT_EQ(w.resources.size(),resources);EXPECT_EQ(w.history.size(),1u);
  EXPECT_EQ(w.current.resource_value,0);EXPECT_EQ(w.normal_collected,0);
}
TEST(Planner, ResourceRouteRejectsDetourStalePickupAndUncoveredRace) {
  for(int scenario=0;scenario<5;++scenario) {
    auto w=resource_pickup_world();
    if(scenario==0) w.resources["near"].pos={8,11}; // Two extra moves to the original goal.
    if(scenario==1) --w.resources["near"].seen;
    if(scenario==2) w.current.radius=3; // A rival just outside view can win the two-step pickup.
    if(scenario==3) w.current.entities.push_back({"robot","B","B",{7,10},100,0,false,true});
    if(scenario==4) w.obstacles[{8,9}]={0,5};
    Planner baseline;PlannerConfig config;config.resource_route_pickups=true;Planner candidate(2,500,config);
    EXPECT_EQ(candidate.decide(w).json(w.current),baseline.decide(w).json(w.current))<<scenario;
    EXPECT_TRUE(candidate.resource_route_evaluation.is_null())<<scenario;
  }
}
TEST(Planner, ResourceRouteAllowsTwoExtraMovesOnlyWithOriginalGoalRaceMargin) {
  auto w=resource_pickup_world();w.resources["near"].pos={8,9};w.resources["goal"].pos={9,8};
  Planner baseline;ASSERT_EQ(baseline.decide(w).direction,"right");
  PlannerConfig config;config.resource_route_pickups=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.direction,"down");
  EXPECT_EQ(candidate.target(),baseline.target());
  const auto &e=candidate.resource_route_evaluation;ASSERT_FALSE(e.is_null());
  EXPECT_EQ(e["steps"],1);EXPECT_EQ(e["via_steps"],3);
  EXPECT_LT(e["via_steps"].get<int>(),e["goal_rival_movement_lower_bound"].get<int>());
  w.current.entities.push_back({"robot","B","B",{11,8},100,0,false,true});
  Planner guarded(2,500,config);guarded.decide(w);EXPECT_TRUE(guarded.resource_route_evaluation.is_null());
}
TEST(Planner, ResourceRoutePreservesStagesEventsAndExistingExperiments) {
  for(int scenario=0;scenario<8;++scenario) {
    auto w=resource_pickup_world();PlannerConfig config;int stage=2;
    if(scenario==0) w.current.tick=40;
    if(scenario==1) stage=1;
    if(scenario==2) w.events.merge(batch(w.current,Json::array({area("purity",1,2)})),w.current);
    if(scenario==3) config.tactical_forecast=true;
    if(scenario==4) config.information_paths=true;
    if(scenario==5) config.resource_beam=true;
    if(scenario==6) config.opening_route_information=true;
    if(scenario==7) {
      w.current.resource_value=120;w.current.completed.insert("high");
    }
    Planner baseline(stage,500,config);config.resource_route_pickups=true;Planner candidate(stage,500,config);
    EXPECT_EQ(candidate.decide(w).json(w.current),baseline.decide(w).json(w.current))<<scenario;
    EXPECT_TRUE(candidate.resource_route_evaluation.is_null())<<scenario;
  }
}
TEST(Planner, ResourceRouteKeepsImmediateFinisherAndClearsDiagnostics) {
  auto w=resource_pickup_world();w.current.entities.push_back({"robot","B","B",{8,6},10,0,false,false});
  Planner baseline;PlannerConfig config;config.resource_route_pickups=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"attack");
  EXPECT_EQ(action.json(w.current),baseline.decide(w).json(w.current));
  EXPECT_FALSE(candidate.resource_route_evaluation.is_null());
  w.current.alive=false;candidate.decide(w);EXPECT_TRUE(candidate.resource_route_evaluation.is_null());
  w.current.alive=true;candidate.decide(w);candidate.reset();EXPECT_TRUE(candidate.resource_route_evaluation.is_null());
}
TEST(Planner, ResourceRaceYieldsToObservedFinalApproachWithoutDeletingMemory) {
  auto w=resource_race_world();Planner baseline;baseline.decide(w);
  ASSERT_EQ(baseline.target(),std::optional<Pos>(Pos{10,8}));
  PlannerConfig config;config.resource_race_yield=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{8,11}));
  EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");
  ASSERT_EQ(candidate.resource_race_evaluations.size(),1u);
  const auto &e=candidate.resource_race_evaluations[0];
  EXPECT_EQ(e["enemy_before"],Json({12,8}));EXPECT_EQ(e["enemy_now"],Json({11,8}));
  EXPECT_EQ(e["own_arrival_lower_bound"],2);EXPECT_EQ(e["enemy_arrival_lower_bound"],1);
  EXPECT_DOUBLE_EQ(e["previous_discount"],1);EXPECT_DOUBLE_EQ(e["approach_discount"],.1);
  EXPECT_EQ(w.resources.size(),2u);EXPECT_EQ(w.resources.at("raced").value,20);
  EXPECT_EQ(w.current.resource_value,0);EXPECT_EQ(w.high_collected,0);
}
TEST(Planner, ResourceRaceNeedsContiguousMovementFreshResourceAndEarlyWindow) {
  for(int scenario=0;scenario<7;++scenario) {
    auto w=resource_race_world();PlannerConfig config;int stage=2;
    if(scenario==0) w.history.pop_front();
    if(scenario==1) w.history.front().entities.back().pos={11,8};
    if(scenario==2) w.history.front().entities.back().pos={13,8};
    if(scenario==3) --w.resources["raced"].seen;
    if(scenario==4) w.current.tick=40;
    if(scenario==5) stage=1;
    if(scenario==6) config.race_aware_resources=true;
    Planner baseline(stage,500,config);config.resource_race_yield=true;Planner candidate(stage,500,config);
    EXPECT_EQ(candidate.decide(w).json(w.current),baseline.decide(w).json(w.current))<<scenario;
    EXPECT_TRUE(candidate.resource_race_evaluations.empty())<<scenario;
  }
}
TEST(Planner, ResourceRaceKeepsPossibleFinisherAndKnownEventContexts) {
  for(int scenario=0;scenario<2;++scenario) {
    auto w=resource_race_world();
    if(scenario==0) {
      w.current.entities.back().pos={9,8};w.current.entities.back().hp=10;
      w.history.front().entities.back().pos={9,9};
    } else w.events.merge(batch(w.current,Json::array({area("purity",1,2)})),w.current);
    Planner baseline;PlannerConfig config;config.resource_race_yield=true;Planner candidate(2,500,config);
    EXPECT_EQ(candidate.decide(w).json(w.current),baseline.decide(w).json(w.current));
    EXPECT_TRUE(candidate.resource_race_evaluations.empty());
  }
}
TEST(Planner, ResourceRaceClearsDiagnosticsAfterDeathOrReset) {
  auto w=resource_race_world();PlannerConfig config;config.resource_race_yield=true;Planner candidate(2,500,config);
  candidate.decide(w);ASSERT_FALSE(candidate.resource_race_evaluations.empty());
  w.current.alive=false;candidate.decide(w);EXPECT_TRUE(candidate.resource_race_evaluations.empty());
  w.current.alive=true;candidate.decide(w);candidate.reset();EXPECT_TRUE(candidate.resource_race_evaluations.empty());
}
TEST(Planner, FreshReturnComparesVisibleResourceAtFullValue) {
  World w;auto f=frame(10);
  // Five moves within the square vision, versus the reachable unknown strip.
  // A four-move axis resource already wins even with the old 0.6 discount.
  f.entities.push_back({"resource_normal","return","",{7,4},100,10,false,false});
  w.update(f);w.spawn=Pos{3,4};
  Planner baseline;baseline.decide(w);EXPECT_EQ(baseline.goal_reason,"explore");
  PlannerConfig config;config.resource_fresh_returns=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);
  EXPECT_EQ(candidate.target(),std::optional<Pos>({7,4}));EXPECT_EQ(candidate.goal_reason,"resource");
  EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"up");
  EXPECT_EQ(w.resources.size(),1u);EXPECT_EQ(w.current.resource_value,0);
}
TEST(Planner, FreshReturnPreservesStaleLateEventAndFirstStageBehavior) {
  for(int scenario=0;scenario<5;++scenario) {
    World w;auto f=frame(scenario==1?40:10);
    f.entities.push_back({"resource_normal","return","",{8,4},100,10,false,false});
    w.update(f);w.spawn=Pos{3,4};int stage=scenario==2?1:2;
    if(scenario==0) w.resources.begin()->second.seen=f.tick-1;
    if(scenario==3) w.events.merge(batch(f,Json::array({area("purity",30,50)})),f);
    PlannerConfig config;config.resource_fresh_returns=true;
    if(scenario==4) config.tactical_forecast=true;
    auto control=config;control.resource_fresh_returns=false;
    Planner baseline(stage,500,control),candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(f),candidate.decide(w).json(f))<<scenario;
    EXPECT_EQ(baseline.target(),candidate.target())<<scenario;
  }
}
namespace {
Frame blocked_resource_frame(int tick=10) {
  auto f=frame(tick);f.resource_value=50;
  f.entities={{"resource_normal","contested","",{9,8},100,10,false,false},
    {"resource_normal","alternative","",{8,12},100,10,false,false},
    {"robot","B","B",{10,8},100,0,false,false},
    {"robot","C","C",{9,7},100,0,false,false}};
  f.missions.push_back({{"id","normal"},{"type","normal_resources"},{"target",2}});
  return f;
}
void blocked_receipt(Frame &f) {
  ++f.tick;f.action_tick=f.tick-1;f.action_type="move";f.action_error="E_BLOCKED";f.moved=false;
}
void prime_blocked_resource(World &w,Planner &p,Frame &f) {
  w.update(f);w.normal_collected=1;w.high_collected=2;
  EXPECT_EQ(p.decide(w).direction,"right");
  blocked_receipt(f);w.update(f);EXPECT_EQ(p.decide(w).direction,"right");
  blocked_receipt(f);w.update(f);
}
}
TEST(Perception, OptionalFailureReceiptDoesNotInventADestinationOrRejectLegacyInput) {
  auto j=perception(12);j["self"]["last_action"]={{"tick",11},{"action","move"},{"error","E_BLOCKED"},{"moved",false}};
  auto f=parse_frame(j);EXPECT_EQ(f.action_type,"move");EXPECT_EQ(f.action_error,"E_BLOCKED");EXPECT_FALSE(f.moved);
  j["self"]["last_action"].erase("error");EXPECT_TRUE(parse_frame(j).action_error.empty());
  j["self"]["last_action"]["error"]=42;EXPECT_TRUE(parse_frame(j).action_error.empty());
}
TEST(Planner, BlockedRecoveryUsesConfirmedFailuresAndCompletesAVisibleAlternative) {
  PlannerConfig c;c.resource_blocked_recovery=true;Planner p(2,500,c);World w;auto f=blocked_resource_frame();
  prime_blocked_resource(w,p,f);auto remembered=w.resources.size();
  auto a=p.decide(w);ASSERT_EQ(a.type,"move");ASSERT_EQ(a.direction,"down");
  EXPECT_EQ(p.resource_recovery_evaluation["status"],"started");
  EXPECT_EQ(p.resource_recovery_evaluation["failed_receipts"],2);
  EXPECT_EQ(p.resource_recovery_evaluation["steps"],4);
  EXPECT_EQ(p.resource_recovery_evaluation["visible_rival_distance"],6);
  EXPECT_EQ(w.resources.size(),remembered);EXPECT_EQ(w.current.resource_value,50);
  for(int i=1;i<4;++i) {
    ++f.tick;++f.pos.y;f.action_tick=f.tick-1;f.moved=true;f.action_error.clear();w.update(f);
    a=p.decide(w);EXPECT_EQ(a.type,"move");EXPECT_EQ(a.direction,"down")<<i;
    EXPECT_EQ(p.resource_recovery_evaluation["status"],"continued");
  }
  ++f.tick;++f.pos.y;f.action_tick=f.tick-1;f.resource_value=60;f.entities.erase(f.entities.begin()+1);
  f.completed.insert("normal");w.update(f);p.decide(w);
  EXPECT_EQ(w.normal_collected,2);EXPECT_EQ(w.current.resource_value,60);
  EXPECT_EQ(p.resource_recovery_evaluation["status"],"cancelled");
}
TEST(Planner, BlockedRecoveryNeedsMatchingContiguousOwnReceipts) {
  for(int scenario=0;scenario<7;++scenario) {
    PlannerConfig c;c.resource_blocked_recovery=true;Planner p(2,500,c);World w;auto f=blocked_resource_frame();
    prime_blocked_resource(w,p,f);
    if(scenario==0) w.current.action_error.clear();
    if(scenario==1) w.current.action_error="E_INVALID";
    if(scenario==2) w.current.action_type="wait";
    if(scenario==3) --w.current.action_tick;
    if(scenario==4) ++w.current.tick;
    if(scenario==5) w.current.moved=true;
    if(scenario==6) ++w.current.pos.y;
    p.decide(w);EXPECT_TRUE(p.resource_recovery_evaluation.is_null())<<scenario;
  }
}
TEST(Planner, BlockedRecoveryDoesNotCountSameTickRevisionTwiceAndResetsSession) {
  PlannerConfig c;c.resource_blocked_recovery=true;Planner p(2,500,c);World w;auto f=blocked_resource_frame();
  w.update(f);w.normal_collected=1;p.decide(w);blocked_receipt(f);w.update(f);
  for(int i=0;i<3;++i) {p.decide(w);EXPECT_TRUE(p.resource_recovery_evaluation.is_null());}
  blocked_receipt(f);w.update(f);p.decide(w);ASSERT_EQ(p.resource_recovery_evaluation["status"],"started");
  p.reset();p.decide(w);EXPECT_TRUE(p.resource_recovery_evaluation.is_null());
  f.session="another-match";f.tick=0;w.update(f);p.decide(w);EXPECT_TRUE(p.resource_recovery_evaluation.is_null());
}
TEST(Planner, BlockedRecoveryRejectsAbsentStaleOccupiedOrUnwinnableAlternatives) {
  for(int scenario=0;scenario<6;++scenario) {
    PlannerConfig c;c.resource_blocked_recovery=true;Planner p(2,500,c);World w;auto f=blocked_resource_frame();
    prime_blocked_resource(w,p,f);
    if(scenario==0) w.resources.erase("alternative");
    if(scenario==1) --w.resources["alternative"].seen;
    if(scenario==2) w.current.entities.push_back({"robot","D","D",{8,12},100,0,false,true});
    if(scenario==3) w.current.entities.push_back({"robot","D","D",{12,12},100,0,false,true});
    if(scenario==4) for(auto q:std::vector<Pos>{{8,11},{7,12},{9,12},{8,13}}) w.obstacles[q]={0,f.tick};
    if(scenario==5) w.current.entities.erase(w.current.entities.begin()+2,w.current.entities.end());
    p.decide(w);EXPECT_NE(p.resource_recovery_evaluation.value("status",std::string()),"started")<<scenario;
  }
}
TEST(Planner, BlockedRecoveryPreservesCombatAndEventStageBoundaries) {
  for(int scenario=0;scenario<6;++scenario) {
    PlannerConfig c;c.resource_blocked_recovery=true;int stage=scenario==0?1:2;
    Planner p(stage,500,c);World w;auto f=blocked_resource_frame();
    if(stage==1) {w.update(f);p.decide(w);blocked_receipt(f);w.update(f);p.decide(w);blocked_receipt(f);w.update(f);}
    else prime_blocked_resource(w,p,f);
    if(scenario==1) w.current.tick=100;
    if(scenario==2) w.events.merge(batch(w.current,Json::array({area("purity",20,30)})),w.current);
    if(scenario==3) w.current.alive=false;
    if(scenario==4) w.current.entities[2].hp=10;
    if(scenario==5) w.current.hp=15; // Defense survives 10 damage; moving would take 20.
    auto a=p.decide(w);
    EXPECT_TRUE(p.resource_recovery_evaluation.is_null() || p.resource_recovery_evaluation["status"]!="started")<<scenario;
    if(scenario==4) {EXPECT_EQ(a.type,"attack");}
    if(scenario==5) {EXPECT_NE(a.type,"move");}
  }
}
TEST(Planner, BlockedRecoveryCancelsOnNewFailureDisappearanceOrLostRace) {
  for(int scenario=0;scenario<4;++scenario) {
    PlannerConfig c;c.resource_blocked_recovery=true;Planner p(2,500,c);World w;auto f=blocked_resource_frame();
    prime_blocked_resource(w,p,f);p.decide(w);ASSERT_EQ(p.resource_recovery_evaluation["status"],"started");
    ++f.tick;f.action_tick=f.tick-1;f.moved=scenario!=0;f.action_error=scenario==0?"E_BLOCKED":"";
    if(f.moved) ++f.pos.y;
    if(scenario==1) f.entities.erase(f.entities.begin()+1);
    if(scenario==2) f.entities.push_back({"robot","D","D",{8,13},100,0,false,true});
    if(scenario==3) {f.tick+=10;f.action_tick=f.tick-1;}
    w.update(f);p.decide(w);EXPECT_EQ(p.resource_recovery_evaluation["status"],"cancelled")<<scenario;
  }
}
TEST(Planner, CompetitiveExplorationPrefersLessContestedUnknownStripWithoutChangingMemory) {
  auto w=exploration_race_world();
  PlannerConfig config;config.competitive_exploration=false;Planner baseline(2,500,config);
  auto old_action=baseline.decide(w);EXPECT_EQ(old_action.type,"move");EXPECT_EQ(old_action.direction,"left");
  EXPECT_EQ(baseline.target(),std::optional<Pos>(Pos{7,8}));
  auto explored=w.explored;auto obstacles=w.obstacles.size();auto history=w.history.size();
  config.competitive_exploration=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"right");
  EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{9,8}));
  EXPECT_EQ(w.explored,explored);EXPECT_EQ(w.obstacles.size(),obstacles);EXPECT_EQ(w.history.size(),history);
  EXPECT_TRUE(w.resources.empty());EXPECT_EQ(w.current.resource_value,0);EXPECT_EQ(w.enemies.size(),1u);
}
TEST(Planner, SurvivalConditionedProgressDoesNotCreditAMoveCancelledByPredictedCombatDeath) {
  World w;auto f=frame(200);f.hp=10;f.effective_damage=200;
  f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});
  f.missions.push_back({{"id","reach"},{"type","reach"},{"position",{8,9}}});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  PlannerConfig config;config.survival_conditioned_progress=false;Planner baseline(2,500,config);
  auto old_action=baseline.decide(w);EXPECT_EQ(old_action.type,"move");EXPECT_EQ(old_action.direction,"down");
  auto explored=w.explored;auto history=w.history.size();
  config.survival_conditioned_progress=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"right");
  EXPECT_EQ(w.current.pos,(Pos{8,8}));EXPECT_EQ(w.current.hp,10);EXPECT_EQ(w.current.effective_damage,200);
  EXPECT_EQ(w.explored,explored);EXPECT_EQ(w.history.size(),history);EXPECT_TRUE(w.current.completed.empty());
}
TEST(Planner, SafeProgressMovesAvoidPossibleOneStepFireWithoutLengtheningKnownRoute) {
  auto w=fire_choice_world();PlannerConfig config;config.safe_progress_moves=false;Planner baseline(2,500,config);
  auto old_action=baseline.decide(w);EXPECT_EQ(old_action.type,"move");EXPECT_EQ(old_action.direction,"left");
  auto explored=w.explored;auto history=w.history.size();
  config.safe_progress_moves=true;Planner candidate(2,500,config);auto action=candidate.decide(w);
  EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");EXPECT_EQ(candidate.target(),baseline.target());
  EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{14,7}));EXPECT_EQ(w.explored,explored);EXPECT_EQ(w.history.size(),history);
  EXPECT_EQ(w.current.pos,(Pos{16,5}));EXPECT_EQ(w.current.hp,40);EXPECT_EQ(w.current.effective_damage,200);
}
TEST(Planner, SafeProgressMovesPreserveCoverageCurrentCombatAndRouteBoundaries) {
  for(int scenario=0;scenario<12;++scenario) {
    auto w=fire_choice_world(scenario==1?80:200,scenario==5);auto f=w.current;
    if(scenario==2) f.radius=3;
    if(scenario==3) f.protected_spawn=true;
    if(scenario==4) f.hp=100;
    if(scenario==6 || scenario==7) {f.entities[0].pos={14,5};f.hp=scenario==6?20:40;}
    if(scenario==7) f.entities[0].hp=10;
    if(scenario==10) f.entities.clear();
    ++f.tick;w.update(f);
    if(scenario==8) {
      Json split={{"kind","world_split"},{"start_tick",f.tick},{"end_tick",f.tick+30},{"points",{{0,0}}},{"params",Json::object()}};
      w.events.merge(batch(f,Json::array({split})),f);
    }
    if(scenario==9) w.events.merge(batch(f,Json::array({area("unrivaled",f.tick,f.tick+50,{{"attack_radius",3}})})),f);
    if(scenario==11) {
      auto blast=area("explosion",f.tick+1,f.tick+2,{{"damage",80}});blast["rect"]={{16,6},{16,6}};
      w.events.merge(batch(f,Json::array({blast})),f);
    }
    int stage=scenario==0?1:2;PlannerConfig config;config.safe_progress_moves=false;Planner baseline(stage,500,config);
    config.safe_progress_moves=true;Planner candidate(stage,500,config);
    auto old_action=baseline.decide(w),action=candidate.decide(w);
    EXPECT_EQ(old_action.json(f),action.json(f))<<scenario;EXPECT_EQ(baseline.target(),candidate.target())<<scenario;
    if(scenario==7) {EXPECT_EQ(action.type,"attack");}
  }
}
TEST(Planner, SafeProgressMovesDoNotTreatWallsAsNormalShotBlockers) {
  auto w=fire_choice_world();auto f=w.current;++f.tick;
  f.entities[0].pos={14,3};f.entities[1].pos={18,3};
  f.entities.push_back({"obstacle","","",{15,4},100,0,false,false});w.update(f);
  PlannerConfig config;config.safe_progress_moves=false;Planner baseline(2,500,config);
  auto old_action=baseline.decide(w);EXPECT_EQ(old_action.type,"move");EXPECT_EQ(old_action.direction,"left");
  config.safe_progress_moves=true;Planner candidate(2,500,config);auto action=candidate.decide(w);
  EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");
}
TEST(Planner, SafeProgressMovesAllowCurrentlyProtectedEnemiesToFireAfterProtectionExpires) {
  auto w=fire_choice_world();auto f=w.current;++f.tick;
  for(auto &enemy:f.entities) {enemy.protected_spawn=true;}
  w.update(f);
  PlannerConfig config;config.safe_progress_moves=true;Planner candidate(2,500,config);auto action=candidate.decide(w);
  EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");
}
TEST(Planner, SurvivalConditionedProgressPreservesSurvivingProtectedAndFirstStagePlans) {
  for(int scenario=0;scenario<5;++scenario) {
    World w;auto f=frame(200);f.hp=scenario==0?100:20;f.effective_damage=200;
    if(scenario==1) f.protected_spawn=true;
    f.entities.push_back({"robot","B","B",scenario==3?Pos{11,8}:Pos{10,8},100,0,false,scenario==2});
    f.missions.push_back({{"id","reach"},{"type","reach"},{"position",{8,9}}});w.update(f);
    for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
    int stage=scenario==4?1:2;PlannerConfig config;config.survival_conditioned_progress=false;
    Planner baseline(stage,500,config);config.survival_conditioned_progress=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(f),candidate.decide(w).json(f))<<scenario;
    EXPECT_EQ(candidate.target(),baseline.target())<<scenario;
  }
}
TEST(Planner, SurvivalConditionedProgressRetainsProductiveMoveWithPartialSurvivalChance) {
  World w;
  for(int tick=196;tick<=200;++tick) {
    auto f=frame(tick);f.hp=10;f.effective_damage=200;
    f.entities.push_back({"robot","B","B",{10,8},100,0,true,false});
    f.missions.push_back({{"id","reach"},{"type","reach"},{"position",{8,9}}});w.update(f);
  }
  for(int x=0;x<w.current.width;++x) for(int y=0;y<w.current.height;++y) w.explored.insert({x,y});
  PlannerConfig config;config.standoff_fire_risk=true;config.survival_conditioned_progress=false;
  Planner baseline(2,500,config);config.survival_conditioned_progress=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"down");
  EXPECT_EQ(action.json(w.current),baseline.decide(w).json(w.current));
}
TEST(Planner, RespawnTaskRouteUsesActualRevivalAndClearsOnCompletionAndReset) {
  auto w=revival_route_world();PlannerConfig config;config.respawn_task_route=false;Planner baseline(2,500,config);
  baseline.decide(w);EXPECT_EQ(baseline.target(),std::optional<Pos>(Pos{8,8}));
  config.respawn_task_route=true;Planner candidate(2,500,config);candidate.decide(w);
  EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{12,12}));
  EXPECT_EQ(w.current.pos,(Pos{8,8}));EXPECT_EQ(w.current.effective_damage,300);EXPECT_TRUE(w.current.completed.empty());
  // A blocked first move leaves the firing cell unchanged. Without an active
  // route, optional hunting wins here; reset must remove that route state.
  auto f=w.current;++f.tick;f.protected_spawn=false;w.update(f);candidate.decide(w);
  EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{12,12}));
  candidate.reset();candidate.decide(w);EXPECT_NE(candidate.target(),std::optional<Pos>(Pos{12,12}));
  auto restarted=revival_route_world();candidate.reset();candidate.decide(restarted);
  f=restarted.current;++f.tick;f.completed.insert("reach");f.protected_spawn=false;restarted.update(f);candidate.decide(restarted);
  EXPECT_NE(candidate.target(),std::optional<Pos>(Pos{12,12}));
}
TEST(Planner, RespawnTaskRouteRequiresLateObservedRevivalCompleteHistoryAndEnoughExploration) {
  for(int scenario=0;scenario<9;++scenario) {
    auto w=revival_route_world(scenario==0?70:(scenario==7?470:(scenario==8?200:490)));
    if(scenario==1) {w.current.protected_spawn=false;w.history.back().protected_spawn=false;}
    if(scenario==2) w.history.erase(w.history.begin()+15);
    if(scenario==3) {w.history[w.history.size()-2].alive=true;w.history[w.history.size()-2].hp=100;}
    if(scenario==4) {w.current.completed.insert("reach");w.history.back().completed.insert("reach");}
    if(scenario==5) {
      w.explored.clear();for(int x=4;x<=12;++x) for(int y=4;y<=12;++y) w.explored.insert({x,y});
    }
    if(scenario==7) {
      w.history.front().effective_damage=0;
      for(auto &past:w.history) past.missions[0]["position"]={22,22};
      w.current.missions[0]["position"]={22,22};
    }
    int stage=scenario==6?1:2;PlannerConfig config;config.respawn_task_route=false;Planner baseline(stage,500,config);
    config.respawn_task_route=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current))<<scenario;
    EXPECT_EQ(baseline.target(),candidate.target())<<scenario;
  }
}
TEST(Planner, RespawnTaskRouteKeepsMoreValuableKnownResourceBeforePublicTask) {
  auto w=revival_route_world(490,true);PlannerConfig config;config.respawn_task_route=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(candidate.reason,"resource");
  EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{9,8}));EXPECT_EQ(w.resources.size(),1u);
  auto f=w.current;++f.tick;f.pos={9,8};f.resource_value=20;f.protected_spawn=false;f.entities.erase(f.entities.begin()+1);w.update(f);
  candidate.decide(w);EXPECT_EQ(candidate.target(),std::optional<Pos>(Pos{12,12}));
}
TEST(Planner, RespawnTaskRouteClearsOnWithdrawalKnownBlockDeadlineOrNewDeath) {
  for(int scenario=0;scenario<4;++scenario) {
    auto w=revival_route_world();PlannerConfig config;config.respawn_task_route=true;Planner candidate(2,500,config);
    Pos objective=scenario==3?Pos{11,11}:Pos{12,12};
    if(scenario==3) {
      for(auto &past:w.history) past.missions[0]["position"]={11,11};
      w.current.missions[0]["position"]={11,11};
    }
    candidate.decide(w);ASSERT_EQ(candidate.target(),std::optional<Pos>(objective));
    auto f=w.current;f.tick=scenario==2?499:491;f.protected_spawn=false;
    if(scenario==0) f.missions.clear();
    if(scenario==3) {f.alive=false;f.hp=0;}
    w.update(f);
    if(scenario==1) {
      Json split={{"kind","world_split"},{"start_tick",491},{"end_tick",531},{"points",{{12,0}}},{"params",Json::object()}};
      w.events.merge(batch(f,Json::array({split})),f);
    }
    candidate.decide(w);
    if(scenario==3) {
      f.tick=495;f.alive=true;f.hp=100;f.pos={9,8};w.update(f);candidate.decide(w);
    }
    EXPECT_NE(candidate.target(),std::optional<Pos>(objective))<<scenario;
  }
}
TEST(Planner, CompetitiveExplorationPreservesKnownResourceLateStageStaleAndBlockedRivalCases) {
  for(int scenario=0;scenario<6;++scenario) {
    auto w=exploration_race_world(scenario==1?100:20,scenario!=5);
    if(scenario==2 || scenario==3 || scenario==4) {
      auto f=w.current;++f.tick;
      if(scenario==2) f.entities.push_back({"resource_normal","n","",{9,8},100,10,false,false});
      if(scenario==3) f.entities.clear(); // The remembered rival is no longer current.
      if(scenario==4) {
        w.obstacles[{3,8}]={19,19};w.explored.insert({3,8});
        for(Pos p:std::array<Pos,3>{{{5,8},{4,7},{4,9}}})
          f.entities.push_back({"obstacle","","",p,100,0,false,false});
      }
      w.update(f);
    }
    int stage=scenario==0?1:2;PlannerConfig config;config.competitive_exploration=false;
    Planner baseline(stage,500,config);config.competitive_exploration=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(w.current),candidate.decide(w).json(w.current))<<scenario;
    EXPECT_EQ(baseline.target(),candidate.target())<<scenario;
  }
}
TEST(Planner, FinishEarlyExplorationRetainsReachableFrontierBeforeOptionalHunt) {
  auto w=exploration_race_world(20,false);auto f=w.current;++f.tick;
  f.entities.push_back({"robot","B","B",{11,8},100,0,false,false});w.update(f);
  PlannerConfig config;config.finish_early_exploration=false;Planner baseline(2,500,config);
  auto old_action=baseline.decide(w);EXPECT_EQ(old_action.type,"move");EXPECT_EQ(baseline.reason,"engage");
  auto explored=w.explored;auto history=w.history.size();
  config.finish_early_exploration=true;Planner candidate(2,500,config);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(candidate.reason,"explore");
  EXPECT_NE(candidate.target(),baseline.target());EXPECT_EQ(action.direction,"left");
  EXPECT_EQ(w.explored,explored);EXPECT_EQ(w.history.size(),history);EXPECT_TRUE(w.resources.empty());
}
TEST(Planner, FinishEarlyExplorationPreservesImmediateCombatKnownResourcesLateAndCompleteMap) {
  for(int scenario=0;scenario<5;++scenario) {
    auto w=exploration_race_world(scenario==2?100:20,false);auto f=w.current;++f.tick;
    f.entities.push_back({"robot","B","B",scenario==0?Pos{10,8}:Pos{11,8},scenario==0?10.0:100.0,0,false,false});
    if(scenario==1) f.entities.push_back({"resource_normal","n","",{7,8},100,10,false,false});
    w.update(f);
    if(scenario==3) for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
    int stage=scenario==4?1:2;PlannerConfig config;config.finish_early_exploration=false;
    Planner baseline(stage,500,config);config.finish_early_exploration=true;Planner candidate(stage,500,config);
    auto old_action=baseline.decide(w),action=candidate.decide(w);
    EXPECT_EQ(old_action.json(f),action.json(f))<<scenario;
    if(scenario==0) {EXPECT_EQ(action.type,"attack");}
  }
}
TEST(Planner, ContestedTaskResourceRequiresCurrentRivalLeadPublicTaskAndEarlyWindow) {
  for(int scenario=0;scenario<5;++scenario) {
    World w;auto f=frame(scenario==0?97:10);
    f.resource_value=20;f.completed.insert("high");
    if(scenario!=1) f.missions.push_back({{"id","high"},{"type","high_resources"},{"target",1}});
    f.entities.push_back({"resource_high","safe","",{6,8},100,20,false,false});
    f.entities.push_back({"resource_high","contested","",{10,9},100,20,false,false});
    f.entities.push_back({"robot","B","B",scenario==2?Pos{11,9}:Pos{12,11},100,0,false,true});w.update(f);
    if(scenario==3) {++f.tick;f.entities.pop_back();w.update(f);}
    for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
    PlannerConfig config;config.resource_lookahead=1;
    int stage=scenario==4?1:2;Planner baseline(stage,500,config);
    config.contested_task_resources=true;Planner candidate(stage,500,config);
    EXPECT_EQ(baseline.decide(w).json(f),candidate.decide(w).json(f))<<scenario;
    EXPECT_EQ(baseline.target(),candidate.target())<<scenario;
  }
}
TEST(Planner, BeamCreditsResourcesAlongTheRouteWithoutChangingMemory) {
  World w;auto f=frame();
  for(int x=9;x<=11;++x) f.entities.push_back({"resource_normal",std::to_string(x),"",{x,8},100,10,false,false});
  f.entities.push_back({"resource_high","h","",{8,11},100,20,false,false});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  PlannerConfig config;config.resource_beam=true;Planner p(2,500,config);
  auto action=p.decide(w);EXPECT_EQ(action.type,"move");EXPECT_EQ(action.direction,"right");
  EXPECT_EQ(w.resources.size(),4u);EXPECT_EQ(w.current.resource_value,0);EXPECT_EQ(w.normal_collected,0);
}
TEST(Planner, BeamPreservesUnderfootCollectionAndKnownLethalEventEscape) {
  World w;auto f=frame();f.entities.push_back({"resource_high","h","",f.pos,100,20,false,false});w.update(f);
  PlannerConfig config;config.resource_beam=true;Planner p(2,500,config);EXPECT_NE(p.decide(w).type,"move");
  World danger;f=frame(74);f.pos={4,4};f.entities.push_back({"resource_high","h","",{5,4},100,20,false,false});danger.update(f);
  danger.events.merge(batch(f,Json::array({area("explosion",75,76)})),f);
  auto action=p.decide(danger);EXPECT_EQ(action.type,"move");EXPECT_TRUE(action.direction=="up" || action.direction=="left");
}
TEST(Planner, BeamExplorationDoesNotTurnPredictedVisibilityIntoObservedFacts) {
  World w;w.update(frame());auto observed=w.explored;
  PlannerConfig config;config.resource_beam=true;Planner p(2,500,config);
  EXPECT_EQ(p.decide(w).type,"move");EXPECT_EQ(p.reason,"explore");
  EXPECT_EQ(w.explored,observed);EXPECT_TRUE(w.resources.empty());EXPECT_EQ(w.history.size(),1u);
}
TEST(Planner, EarlyNonlethalDamageCanYieldToExplorationButLateShotsRemain) {
  World w;auto f=frame(10);f.entities.push_back({"robot","B","B",{8,10},100,0,false,false});w.update(f);
  PlannerConfig config;config.productive_escape=false;config.score_aware_combat=false;
  Planner ordinary(2,500,config);EXPECT_EQ(ordinary.decide(w).type,"attack");
  config.early_damage_scale=.25;Planner candidate(2,500,config);
  EXPECT_EQ(candidate.decide(w).type,"move");
  f.tick=200;w.update(f);auto action=candidate.decide(w);
  EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"down");
}
TEST(Planner, DisprovedEnemyPositionStopsEngagementAndFallbackPursuit) {
  World w;auto f=frame(200);f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  PlannerConfig config;config.validated_enemy_memory=true;Planner candidate(2,500,config);
  f.tick=201;f.entities.clear();w.update(f);
  Planner old;old.decide(w);EXPECT_EQ(old.reason,"engage");
  candidate.decide(w);EXPECT_EQ(candidate.reason,"patrol");
  EXPECT_EQ(w.enemies.at("B").second,200); // Keep history, invalidate only the location hypothesis.
  f.tick=207;w.update(f);old.decide(w);EXPECT_EQ(old.reason,"pursue");
  candidate.decide(w);EXPECT_EQ(candidate.reason,"patrol");
  f.tick=208;f.entities.push_back({"robot","B","B",{8,10},100,0,false,false});w.update(f);
  auto action=candidate.decide(w);EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"down");
  EXPECT_EQ(w.enemies.at("B").second,208);
}
TEST(Planner, EnemyMemoryNeedsLiveVisibilityAndStaysDisprovedAfterLeaving) {
  World w;auto f=frame(200);f.entities.push_back({"robot","B","B",{10,8},100,0,false,false});w.update(f);
  for(int x=0;x<f.width;++x) for(int y=0;y<f.height;++y) w.explored.insert({x,y});
  PlannerConfig config;config.validated_enemy_memory=true;Planner candidate(2,500,config);
  f.tick=201;f.entities.clear();f.alive=false;w.update(f);
  f.tick=202;f.alive=true;f.pos={4,8};w.update(f);
  candidate.decide(w);EXPECT_EQ(candidate.reason,"engage"); // The old cell is outside live vision.
  f.tick=203;f.pos={8,8};w.update(f);candidate.decide(w);EXPECT_EQ(candidate.reason,"patrol");
  f.tick=204;f.pos={4,8};w.update(f);candidate.decide(w);EXPECT_EQ(candidate.reason,"patrol");
}
TEST(Planner, DiscountPreservesPotentialFinishersAndNecessaryDefense) {
  PlannerConfig config;config.early_damage_scale=0;
  for(double hp:{15.0,20.0}) {
    World w;auto f=frame(10);f.entities.push_back({"robot","B","B",{8,10},hp,0,false,false});w.update(f);
    Planner candidate(2,500,config);auto action=candidate.decide(w);
    EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"down");
  }
  World w;auto f=frame(10);f.hp=15;f.entities.push_back({"robot","B","B",{8,10},100,0,false,false});w.update(f);
  Planner candidate(2,500,config);EXPECT_EQ(candidate.decide(w).type,"defend");
}
TEST(Planner, RejectsInvalidDamageValuationParameters) {
  PlannerConfig config;config.early_damage_scale=-.1;EXPECT_THROW(Planner(2,500,config),std::exception);
  config.early_damage_scale=1.1;EXPECT_THROW(Planner(2,500,config),std::exception);
  config.early_damage_scale=.25;config.damage_scale_ticks=501;EXPECT_THROW(Planner(2,500,config),std::exception);
}
