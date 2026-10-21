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
TEST(Events, AtomicSchemaAndSessionValidation) {
  World w;w.update(frame());auto valid=area("explosion",50,51,{{"damage",35}});
  auto bad=valid;bad["kind"]="unknown";
  EXPECT_THROW(w.events.merge(batch(w.current,Json::array({valid,bad})),w.current),std::exception);
  EXPECT_TRUE(w.events.items.empty());
  auto wrong=batch(w.current);wrong["session_id"]="old";EXPECT_THROW(w.events.merge(wrong,w.current),std::exception);
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
