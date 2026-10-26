#include "rmtc_bot/core.hpp"
#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>

namespace rmtc {
namespace {
constexpr int capacity=400, unreachable=1000000;
constexpr std::array<Pos,4> directions{{{0,-1},{1,0},{0,1},{-1,0}}};
constexpr std::array<const char*,4> names{{"up","right","down","left"}};
using Grid=std::array<int,capacity>;
struct Search {Grid distance{},parent{},first{};};
struct Hypothesis {Grid wall{},resource{};};
struct Macro {std::vector<int> path;std::string kind;double bank{};int return_steps{};std::vector<double> outcomes;
  int branch_child{-1},closed_cells{},new_view_cells{},marginal_view_cells{};
  double adverse_reference{},adverse_outcome{};bool adverse{};};
Search search(int width,int height,int root,const Grid &wall,const Grid *observed=nullptr) {
  Search result;result.distance.fill(unreachable);result.parent.fill(-1);result.first.fill(-1);
  Grid queue{};int read=0,write=0;queue[write++]=root;result.distance[root]=0;
  while(read<write) {
    int cell=queue[read++],x=cell%width,y=cell/width;
    for(int d=0;d<4;++d){int xx=x+directions[d].x,yy=y+directions[d].y;
      if(xx<0 || yy<0 || xx>=width || yy>=height)continue;
      int next=yy*width+xx;if(wall[next] || (observed && !(*observed)[next]) || result.distance[next]!=unreachable)continue;
      result.distance[next]=result.distance[cell]+1;result.parent[next]=cell;
      result.first[next]=cell==root?d:result.first[cell];queue[write++]=next;
    }
  }
  return result;
}
std::vector<int> path_to(const Search &s,int root,int goal) {
  std::vector<int> path;if(s.distance[goal]==unreachable)return path;
  for(int cell=goal;cell!=root;cell=s.parent[cell])path.push_back(cell);
  std::reverse(path.begin(),path.end());return path;
}
// Fully observed routes give feasible collection values, not a prediction that
// unknown space is empty. Exact for <=12 remaining observed resources; larger
// inventories use feasible multistart greedy tours and are labelled accordingly.
struct KnownPlan {int value{},steps{};std::vector<int> order;};
struct KnownBank {
  int width{},height{},n{};std::vector<int> cells,values;std::vector<Search> from;
  std::vector<int> reward,tour;bool exact{};
  KnownBank(int w,int h,const Grid &observed,const Grid &wall,const Grid &resources):width(w),height(h) {
    for(int i=0;i<w*h;++i)if(resources[i]){cells.push_back(i);values.push_back(resources[i]);from.push_back(search(w,h,i,wall,&observed));}
    n=cells.size();exact=n<=12;if(!exact)return;
    int count=1<<n;reward.resize(count);tour.assign(count*n,unreachable);
    for(int m=1;m<count;++m){int i=__builtin_ctz(unsigned(m));reward[m]=reward[m^(1<<i)]+values[i];
      for(int bits=m;bits;bits&=bits-1){int first=__builtin_ctz(unsigned(bits)),rest=m^(1<<first);
        if(!rest){tour[m*n+first]=0;continue;}
        for(int tail=rest;tail;tail&=tail-1){int next=__builtin_ctz(unsigned(tail));
          tour[m*n+first]=std::min(tour[m*n+first],from[first].distance[cells[next]]+tour[rest*n+next]);}
      }
    }
  }
  int query(int origin,int budget,int taken,KnownPlan *plan=nullptr) const {
    if(plan)*plan={};
    int best=0,best_steps=unreachable;if(budget<0)return 0;
    if(exact){int allowed=((1<<n)-1)^taken;
      int best_mask=0,best_first=-1;
      for(int m=allowed;m;m=(m-1)&allowed){if(reward[m]<best || (!plan && reward[m]==best))continue;
        for(int bits=m;bits;bits&=bits-1){int first=__builtin_ctz(unsigned(bits));
          int steps=from[first].distance[origin]+tour[m*n+first];
          if(steps<=budget && (reward[m]>best || steps<best_steps)){
            best=reward[m];best_steps=steps;best_mask=m;best_first=first;
            if(!plan)break;
          }}}
      if(plan && best_first>=0){plan->value=best;plan->steps=best_steps;
        int mask=best_mask,first=best_first;
        while(mask){plan->order.push_back(cells[first]);int rest=mask^(1<<first),next=-1;
          for(int bits=rest;bits;bits&=bits-1){int i=__builtin_ctz(unsigned(bits));
            if(from[first].distance[cells[i]]+tour[rest*n+i]==tour[mask*n+first]){next=i;break;}}
          mask=rest;first=next;
        }
      }
      return best;
    }
    for(int seed=0;seed<n;++seed){int used=taken,position=origin,cost=0,value=0;
      std::vector<int> order;
      for(int step=0;step<n;++step){int next=-1;double rate=-1;
        for(int i=0;i<n;++i)if(!(used&(1<<i))){int d=from[i].distance[position];if(cost+d>budget)continue;
          double r=double(values[i])/std::max(1,d);if(!step && i==seed)r=1e9;
          if(r>rate){rate=r;next=i;}}
        if(next<0)break;
        cost+=from[next].distance[position];position=cells[next];used|=1<<next;value+=values[next];
        if(plan)order.push_back(position);
        if(value>best || (value==best && cost<best_steps)){best=value;best_steps=cost;
          if(plan)*plan={best,cost,order};}
      }
    }
    return best;
  }
};
struct RolloutGeometry {
  struct View {int x0{},x1{},y0{},y1{},a{},b{},c{},d{};};
  int width{},height{};std::array<View,capacity> views{};
  RolloutGeometry(int w,int h,int radius):width(w),height(h) {
    for(int cell=0;cell<w*h;++cell){int x=cell%w,y=cell/w;auto &v=views[cell];
      v.x0=std::max(0,x-radius);v.x1=std::min(w,x+radius+1);
      v.y0=std::max(0,y-radius);v.y1=std::min(h,y+radius+1);
      v.a=v.y1*(w+1)+v.x1;v.b=v.y0*(w+1)+v.x1;v.c=v.y1*(w+1)+v.x0;v.d=v.y0*(w+1)+v.x0;}
  }
};
// A compact simulated policy observes only its own square sensor footprint in
// a hypothetical world. The hypothetical map is never supplied to its planner.
double rollout(const Hypothesis &map,const Macro &macro,const RolloutGeometry &geometry,int root,int budget,
               Grid observed,Grid walls,Grid resources,Grid visits) {
  const int width=geometry.width,height=geometry.height;
  int position=root;double earned=0;size_t cursor=0;bool committed=true;
  static const auto discount=[](){std::array<double,capacity+1> values{};
    for(int d=0;d<=capacity;++d){values[d]=1/std::pow(d+1,.85);}
    return values;}();
  Grid taken{},view_gain{};std::array<double,capacity> view_reward{};bool coverage_dirty=true;
  auto observe=[&](){const auto &v=geometry.views[position];
    for(int y=v.y0;y<v.y1;++y)
      for(int x=v.x0;x<v.x1;++x){int cell=y*width+x;
        coverage_dirty=coverage_dirty || !observed[cell];
        observed[cell]=1;walls[cell]=map.wall[cell];resources[cell]=taken[cell]?0:map.resource[cell];}
  };
  for(int tick=0;tick<budget;++tick) {
    observe();
    int next=-1;
    if(resources[position])next=position;
    if(next<0 && committed && cursor<macro.path.size()){
      next=macro.path[cursor++];if(walls[next]){committed=false;next=-1;}
    }
    if(next<0){auto travel=search(width,height,position,walls);
      // A static simulated world changes this coverage table only on first
      // observation. Reuse exact counts; resource/visit scores remain live.
      if(coverage_dirty){std::array<int,441> integral{};int stride=width+1;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
          integral[(y+1)*stride+x+1]=!observed[y*width+x]+integral[y*stride+x+1]+integral[(y+1)*stride+x]-integral[y*stride+x];
        for(int cell=0;cell<width*height;++cell){const auto &v=geometry.views[cell];
          view_gain[cell]=integral[v.a]-integral[v.b]-integral[v.c]+integral[v.d];
          view_reward[cell]=2.3*std::min(1.0,view_gain[cell]/20.0);}
        coverage_dirty=false;
      }
      double best=-1;int goal=-1;
      for(int cell=0;cell<width*height;++cell){int d=travel.distance[cell];if(!d || d>budget-tick)continue;
        double value=resources[cell]*.2;
        if(value>0){double score=value*discount[d];if(score>best){best=score;goal=cell;}}
        double score=view_reward[cell]*discount[d];
        // Tiny deterministic tie preference discourages repeated empty cycles.
        score-=visits[cell]*1e-5;
        if(view_gain[cell] && score>best){best=score;goal=cell;}
      }
      if(goal<0)break;
      int d=travel.first[goal];if(d<0)break;
      next=position+directions[d].x+directions[d].y*width;
    }
    // The immediate neighbour was in the simulated sensor footprint. A
    // simulated controller is never permitted to step through an observed wall.
    if(next<0 || next>=width*height || walls[next])break;
    position=next;++visits[position];if(map.resource[position] && !taken[position]){earned+=map.resource[position];taken[position]=1;resources[position]=0;}
  }
  return earned;
}
} // namespace

std::optional<Action> Planner::decide_stage1_subgoal(const World &w) {
  const auto begin=std::chrono::steady_clock::now();const auto &f=w.current;
  const int width=f.width,height=f.height,size=width*height,root=f.pos.y*width+f.pos.x,budget=max_ticks_-f.tick;
  if(width>20 || height>20 || size>capacity || budget<=0 || f.radius<1 || w.resources.size()>16
      || !w.enemies.empty() || !w.events.items.empty() || !w.events.effects.empty())return std::nullopt;
  auto elapsed=[&](){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();};
  Grid observed{},walls{},resources{},visits{};int normal=12-w.normal_collected,high=4-w.high_collected,known_value=0;
  if(std::abs(10*w.normal_collected+20*w.high_collected-f.resource_value)>1e-8)return std::nullopt;
  for(int i=0;i<size;++i){Pos p{i%width,i/width};observed[i]=w.explored.count(p);walls[i]=w.blocked(p,f.tick);
    auto it=w.visits.find(p);if(it!=w.visits.end())visits[i]=it->second;}
  for(const auto &[id,r]:w.resources){(void)id;if(r.value!=10 && r.value!=20)return std::nullopt;
    int cell=r.pos.y*width+r.pos.x;if(cell==root || resources[cell])return std::nullopt;resources[cell]=r.value;
    known_value+=r.value;if(r.value==20)--high;else --normal;}
  if(normal<0 || high<0 || known_value+10*normal+20*high+f.resource_value!=200)return std::nullopt;
  uint64_t signature=1469598103934665603ULL;
  for(int i=0;i<size;++i){signature^=uint64_t(observed[i]+2*walls[i]+4*resources[i]);signature*=1099511628211ULL;}
  signature^=uint64_t(f.resource_value)*1099511628211ULL;
  if(stage1_subgoal_session_!=f.session){stage1_subgoal_goal_.reset();stage1_subgoal_session_=f.session;
    stage1_subgoal_seed_=uint32_t(signature^(signature>>32));}
  // Exact observed-free endgame planning can certify the entire remaining pool.
  // Its incumbent remains protected by the existing controller afterwards.
  if(normal+high==0){if(auto action=decide_stage1(w))return action;}
  auto safe=search(width,height,root,walls,&observed);
  auto issue=[&](int next,const std::string &kind,Pos goal) -> std::optional<Action> {
    target_=goal;target_type_=goal_reason=reason=kind;
    if(next==root)return Action{};
    for(int d=0;d<4;++d)if(next==root+directions[d].x+directions[d].y*width
        && std::abs(next%width-f.pos.x)+std::abs(next/width-f.pos.y)==1)return Action{"move",names[d]};
    return std::nullopt;
  };
  if(stage1_subgoal_goal_ && stage1_subgoal_knowledge_==signature){Pos goal=*stage1_subgoal_goal_;int cell=goal.y*width+goal.x;
    auto path=path_to(safe,root,cell);if(!path.empty() && int(path.size())<=budget){
      stage1_route_evaluation=stage1_subgoal_evaluation_;stage1_route_evaluation["reused_leg"]=true;stage1_route_evaluation["elapsed_ms"]=elapsed();
      return issue(path.front(),resources[cell]?"resource":"explore",goal);}}
  // Reference action is produced by the established controller from the same
  // own World. Future reference rollouts are approximations, not its true score.
  auto reference_config=config_;reference_config.stage1_subgoal_risk=false;reference_config.stage1_resource_route=false;
  Planner reference(1,max_ticks_,reference_config);
  auto old_action=reference.decide(w);int old_next=root;
  if(old_action.type=="move")for(int d=0;d<4;++d)if(old_action.direction==names[d])old_next+=directions[d].x+directions[d].y*width;
  std::vector<Macro> macros;
  auto add=[&](std::vector<int> path,const std::string &kind) -> int {
    if(path.empty() || int(path.size())>budget)return -1;
    for(size_t i=0;i<macros.size();++i)if(macros[i].path==path)return int(i);
    macros.push_back({std::move(path),kind,0,0,{}});return int(macros.size())-1;};
  if(old_next!=root && old_next>=0 && old_next<size && observed[old_next] && !walls[old_next])add({old_next},"reference");
  if(macros.empty())return std::nullopt;
  KnownBank bank(width,height,observed,walls,resources);KnownPlan known_plan;
  int current_bank=bank.query(root,budget,0,&known_plan);
  if(f.resource_value+current_bank>=190){if(auto action=decide_stage1(w))return action;}
  Planner joint=*this;joint.config_.stage1_subgoal_risk=false;
  joint.decide(w);
  if(joint.target()){auto target=*joint.target();int cell=target.y*width+target.x;
    auto path=path_to(safe,root,cell);
    if(path.empty()){
      auto optimistic=search(width,height,root,walls);path=path_to(optimistic,root,cell);
      auto unknown=std::find_if(path.begin(),path.end(),[&](int p){return !observed[p];});path.erase(unknown,path.end());
    }
    add(std::move(path),"joint");
  }
  std::vector<int> resources_by_rate;
  for(int i=0;i<size;++i)if(resources[i] && safe.distance[i]<=budget)resources_by_rate.push_back(i);
  std::stable_sort(resources_by_rate.begin(),resources_by_rate.end(),[&](int a,int b){return double(resources[a])/(safe.distance[a]+1)>double(resources[b])/(safe.distance[b]+1);});
  // Reserve one of the three resource slots for the best feasible multi-resource
  // order. Only its first leg is committed; later observations still replan.
  const size_t resource_limit=macros.size()+3;
  int known_route_candidate=-1;
  if(!known_plan.order.empty())known_route_candidate=add(path_to(safe,root,known_plan.order.front()),"known_route");
  for(int cell:resources_by_rate){if(macros.size()>=resource_limit)break;add(path_to(safe,root,cell),"resource");}
  // Coverage is the union along the actual observed-free BFS path. These masks
  // describe possible new observations, never inferred free cells or resources.
  using ViewMask=std::bitset<capacity>;
  std::array<ViewMask,capacity> path_views{};std::vector<int> viewpoints;
  for(int i=0;i<size;++i)if(i!=root && safe.distance[i]<=budget)viewpoints.push_back(i);
  std::stable_sort(viewpoints.begin(),viewpoints.end(),[&](int a,int b){return safe.distance[a]<safe.distance[b];});
  for(int cell:viewpoints){path_views[cell]=path_views[safe.parent[cell]];
    int x=cell%width,y=cell/width;
    for(int yy=std::max(0,y-f.radius);yy<=std::min(height-1,y+f.radius);++yy)
      for(int xx=std::max(0,x-f.radius);xx<=std::min(width-1,x+f.radius);++xx)
        if(!observed[yy*width+xx])path_views[cell].set(yy*width+xx);
  }
  ViewMask covered;Grid selected{};
  for(const auto &macro:macros)selected[macro.path.back()]=1;
  for(int slot=0;slot<3;++slot){int chosen=-1,gain=0;double best=-1;
    for(int cell:viewpoints)if(!selected[cell]){int marginal=(path_views[cell]&~covered).count();
      double rate=marginal/std::pow(safe.distance[cell]+1,0.75);
      if(marginal && rate>best){chosen=cell;gain=marginal;best=rate;}}
    if(chosen<0)break;
    int index=add(path_to(safe,root,chosen),"probe");macros[index].marginal_view_cells=gain;
    covered|=path_views[chosen];selected[chosen]=1;
  }
  for(auto &macro:macros)macro.new_view_cells=path_views[macro.path.back()].count();
  // Known graph bridges identify a possible need to return through an entrance.
  // A bridge in this partial graph is never asserted to be a true-map dead end.
  Grid entry{},low{},finish{};int timer=0;
  std::function<void(int,int)> dfs=[&](int v,int parent){entry[v]=low[v]=++timer;
    int x=v%width,y=v/width;for(auto d:directions){int xx=x+d.x,yy=y+d.y;if(xx<0 || yy<0 || xx>=width || yy>=height)continue;
      int u=yy*width+xx;if(!observed[u] || walls[u] || u==parent)continue;
      if(!entry[u]){dfs(u,v);low[v]=std::min(low[v],low[u]);}else low[v]=std::min(low[v],entry[u]);}
    finish[v]=timer;};dfs(root,-1);
  for(auto &macro:macros){int taken=0,gain=0;
    for(int cell:macro.path)for(int i=0;i<bank.n;++i)if(cell==bank.cells[i] && !(taken&(1<<i))){taken|=1<<i;gain+=bank.values[i];}
    int end=macro.path.back();macro.bank=gain+bank.query(end,budget-int(macro.path.size()),taken);
    int previous=root;
    for(size_t i=0;i<macro.path.size();++i){int cell=macro.path[i];
      if(entry[previous]<entry[cell] && low[cell]>entry[previous] && entry[end]>=entry[cell] && entry[end]<=finish[cell]){
        macro.return_steps=macro.path.size()-i;macro.branch_child=cell;break;}previous=cell;}
  }
  // Every map sample preserves all observed walls, empty cells and resources.
  // The seed depends only on legal geometric observations, never IDs, sessions,
  // test fixture names or the actual hidden map.
  std::mt19937 random(uint32_t(signature^(signature>>32)^uint64_t(root*101+f.tick)));
  double density=(w.obstacles.size()+1.0)/(w.explored.size()+4.0);
  // Keep the original shared sample count while assessing the new candidate
  // routes: changing the count also changes decisions with identical routes.
  constexpr int regular_rollout_budget=64;
  constexpr int scenarios=8;
  const int tail_samples=(scenarios+3)/4;
  const RolloutGeometry geometry(width,height,f.radius);
  Hypothesis first_sample;
  for(int sample=0;sample<scenarios;++sample){Hypothesis map;map.wall=walls;map.resource=resources;std::vector<int> free;
    std::bernoulli_distribution obstacle(density);
    // Refresh conditional samples after new observations. A no-information
    // leg keeps its incumbent, so a fixed small particle set cannot permanently
    // favour the same false hidden-resource hotspots.
    for(int i=0;i<size;++i)if(!observed[i]){map.wall[i]=obstacle(random);if(!map.wall[i])free.push_back(i);}
    std::shuffle(free.begin(),free.end(),random);
    // Rare hypotheses without enough empty cells cannot match the public pool.
    if(int(free.size())<normal+high)return std::nullopt;
    for(int i=0;i<normal+high;++i)map.resource[free[i]]=i<high?20:10;
    if(sample==0)first_sample=map;
    for(auto &macro:macros)macro.outcomes.push_back(rollout(map,macro,geometry,root,budget,observed,walls,resources,visits));
  }
  // Stress a branch having no unseen exit: close only unobserved neighbours
  // of its observed component, then compare both policies on that SAME world.
  // Other unknown regions remain available. This is an explicit adverse
  // hypothesis, not a probability claim or a modification of the actual map.
  struct Pressure {Hypothesis map;int closed{};double reference{};bool valid{};};
  std::map<int,Pressure> pressure_cache;int pressure_reference_rollouts=0,pressure_cache_hits=0,pressure_candidate_rollouts=0;
  for(size_t index=1;index<macros.size();++index){auto &macro=macros[index];if(macro.branch_child<0)continue;
    auto [cached,inserted]=pressure_cache.try_emplace(macro.branch_child);auto &pressure=cached->second;
    if(inserted){pressure.map=first_sample;auto &map=pressure.map;Grid closure{};std::vector<int> displaced;
    for(int v=0;v<size;++v)if(entry[v]>=entry[macro.branch_child] && entry[v]<=finish[macro.branch_child]){
      int x=v%width,y=v/width;for(auto d:directions){int xx=x+d.x,yy=y+d.y;
        if(xx<0 || yy<0 || xx>=width || yy>=height)continue;
        int u=yy*width+xx;
        if(!observed[u])closure[u]=1;}}
    for(int v=0;v<size;++v)if(closure[v]){++pressure.closed;if(map.resource[v])displaced.push_back(map.resource[v]);map.resource[v]=0;map.wall[v]=1;}
    if(pressure.closed){
    std::vector<int> free;for(int v=0;v<size;++v)if(!observed[v] && !map.wall[v] && !map.resource[v])free.push_back(v);
    if(free.size()>=displaced.size()){
    std::mt19937 replacement(stage1_subgoal_seed_^uint32_t(macro.branch_child*2654435761u));std::shuffle(free.begin(),free.end(),replacement);
    for(size_t i=0;i<displaced.size();++i)map.resource[free[i]]=displaced[i];
    pressure.valid=true;++pressure_reference_rollouts;
    pressure.reference=rollout(map,macros[0],geometry,root,budget,observed,walls,resources,visits);
    }}
    }else ++pressure_cache_hits;
    macro.closed_cells=pressure.closed;macro.adverse=pressure.valid;if(!pressure.valid)continue;
    macro.adverse_reference=pressure.reference;++pressure_candidate_rollouts;
    macro.adverse_outcome=rollout(pressure.map,macro,geometry,root,budget,observed,walls,resources,visits);
  }
  int chosen=0;double best=-1e30;Json candidates=Json::array();
  for(size_t i=0;i<macros.size();++i){auto &m=macros[i];double mean=std::accumulate(m.outcomes.begin(),m.outcomes.end(),0.0)/scenarios;
    std::vector<double> gaps;for(int k=0;k<scenarios;++k)gaps.push_back(std::max(0.0,macros[0].outcomes[k]-m.outcomes[k]));
    std::sort(gaps.rbegin(),gaps.rend());double tail=std::accumulate(gaps.begin(),gaps.begin()+tail_samples,0.0)/tail_samples;
    double failure_loss=std::max(0.0,current_bank-m.bank);
    double score=mean-tail-failure_loss*.25;
    double adverse_gap=m.adverse?std::max(0.0,m.adverse_reference-m.adverse_outcome):0;
    bool allowed=i==0 || (gaps[0]<=10 && adverse_gap<=10);
    if(allowed && (score>best+1e-9 || (std::abs(score-best)<1e-9 && m.bank>macros[chosen].bank))){best=score;chosen=i;}
    Json path=Json::array();for(int cell:m.path)path.push_back({cell%width,cell/width});
    candidates.push_back({{"kind",m.kind},{"path",path},{"failure_known_value",m.bank},{"known_graph_return_steps",m.return_steps},
      {"path_new_view_cells",m.new_view_cells},{"marginal_new_view_cells",m.marginal_view_cells},{"branch_child",m.branch_child},
      {"sampled_mean",mean},{"paired_tail_shortfall",tail},{"paired_worst_shortfall",gaps[0]},
      {"modeled_score",score},{"eligible",allowed},{"has_adverse_branch",m.adverse},{"adverse_closed_unknown_cells",m.closed_cells},
      {"adverse_reference_value",m.adverse_reference},{"adverse_value",m.adverse_outcome},{"adverse_shortfall",adverse_gap}});
  }
  const auto &chosen_macro=macros[chosen];int goal=chosen_macro.path.back();
  Json known_order=Json::array();for(int cell:known_plan.order)known_order.push_back({cell%width,cell/width});
  stage1_subgoal_goal_=Pos{goal%width,goal/width};stage1_subgoal_knowledge_=signature;
  stage1_route_evaluation={{"mode","contingent_subgoal"},{"candidates",candidates},{"selected",chosen},
    {"known_bank_value",current_bank},{"bank_solver_exact",bank.exact},{"scenarios",scenarios},
    {"known_bank_order",known_order},{"known_bank_steps",known_plan.steps},{"known_route_candidate",known_route_candidate},
    {"tail_samples",tail_samples},{"regular_rollouts",scenarios*macros.size()},{"regular_rollout_budget",regular_rollout_budget},
    {"pressure_reference_rollouts",pressure_reference_rollouts},{"pressure_cache_hits",pressure_cache_hits},
    {"pressure_candidate_rollouts",pressure_candidate_rollouts},
    {"sample_wall_density",density},{"world_model","own_view_conditioned_hypotheses"},
    {"rollout_model","legal_square_view_greedy_recourse"},{"elapsed_ms",elapsed()}};
  stage1_subgoal_evaluation_=stage1_route_evaluation;
  return issue(chosen_macro.path.front(),resources[goal]?"resource":"explore",*stage1_subgoal_goal_);
}
} // namespace rmtc
