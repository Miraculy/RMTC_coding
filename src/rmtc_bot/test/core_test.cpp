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
Json batch(const Frame &f,Json events=Json::array(),Json effects=Json::array()) {
  return {{"session_id",f.session},{"events",events},{"effects",effects}};
}
Json area(const std::string &kind,int start,int end,Json params=Json::object()) {
  return {{"kind",kind},{"start_tick",start},{"end_tick",end},{"rect",{{4,4},{9,9}}},{"params",params}};
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
  PlannerConfig old_config;old_config.productive_escape=false;Planner baseline(2,500,old_config);
  EXPECT_EQ(baseline.decide(w).type,"attack");
  Planner candidate;
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
TEST(Planner, DefaultCompetitionCueKeepsExploringAndStillTakesAvailableShot) {
  World w;auto f=frame(9);f.entities.push_back({"resource_normal","n","",{8,11},100,10,false,false});w.update(f);
  f.tick=10;f.entities.clear();f.entities.push_back({"robot","B","B",{8,11},100,0,false,false});w.update(f);
  ASSERT_EQ(w.recent_competition.at("B"),10);
  Planner current;EXPECT_EQ(current.decide(w).type,"move");EXPECT_EQ(current.reason,"explore");
  PlannerConfig previous;previous.early_interception=true;Planner opt_in(2,500,previous);
  opt_in.decide(w);EXPECT_EQ(opt_in.reason,"engage");
  f.tick=11;f.entities.front().pos={8,10};w.update(f);auto shot=current.decide(w);
  EXPECT_EQ(shot.type,"attack");EXPECT_EQ(shot.direction,"down");
}
TEST(Planner, DisablingEarlyInterceptionPreservesKnownResourcesAndAvailableShots) {
  World w;auto f=frame(10);
  f.entities.push_back({"robot","B","B",{10,6},100,0,false,false});
  f.entities.push_back({"resource_high","h","",{8,12},100,20,false,false});w.update(f);
  w.recent_competition["B"]=10;
  PlannerConfig config;config.early_interception=false;config.frontier_lookahead=true;Planner p(2,500,config);
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
  Planner ordinary;EXPECT_EQ(ordinary.decide(w).type,"attack");
  PlannerConfig config;config.early_damage_scale=.25;Planner candidate(2,500,config);
  EXPECT_EQ(candidate.decide(w).type,"move");
  f.tick=200;w.update(f);auto action=candidate.decide(w);
  EXPECT_EQ(action.type,"attack");EXPECT_EQ(action.direction,"down");
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
