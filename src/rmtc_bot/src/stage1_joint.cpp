#include "rmtc_bot/core.hpp"
#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>

namespace rmtc {
std::optional<Action> Planner::decide_stage1_joint(const World &w) {
  using Clock=std::chrono::steady_clock;
  const auto start=Clock::now();const auto &f=w.current;
  const int size=f.width*f.height,budget=max_ticks_-f.tick,root=f.pos.y*f.width+f.pos.x,inf=100000000;
  if(size>400 || budget<=0 || f.radius<1 || w.resources.size()>16 || !w.events.items.empty()
      || !w.events.effects.empty() || !w.enemies.empty())return std::nullopt;
  auto elapsed=[&](){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
  double known=0;std::vector<double> value(size);std::vector<int> resources;
  for(const auto &[id,r]:w.resources) {
    (void)id;int cell=r.pos.y*f.width+r.pos.x;known+=r.value;
    if(cell==root)return std::nullopt;
    if(value[cell]==0)resources.push_back(cell);
    value[cell]+=r.value;
  }
  const double unseen=std::max(0.0,200-f.resource_value-known);
  if(unseen<1e-9)return std::nullopt;
  if(stage1_joint_session_!=f.session){stage1_joint_route_.clear();stage1_joint_session_=f.session;}
  constexpr std::array<Pos,4> directions{{{0,-1},{1,0},{0,1},{-1,0}}};
  constexpr std::array<const char*,4> names{{"up","right","down","left"}};
  using Bits=std::bitset<400>;
  std::vector<bool> blocked(size);Bits unknown;
  for(int cell=0;cell<size;++cell) {
    Pos p{cell%f.width,cell/f.width};blocked[cell]=w.blocked(p,f.tick) || w.occupied(p);
    if(!w.explored.count(p))unknown.set(cell);
  }
  const int unknown_count=unknown.count();if(!unknown_count)return std::nullopt;
  std::vector<Pos> resource_points,wall_points;
  for(int cell:resources)resource_points.push_back({cell%f.width,cell/f.width});
  std::sort(resource_points.begin(),resource_points.end());
  for(int cell=0;cell<size;++cell)if(blocked[cell])wall_points.push_back({cell%f.width,cell/f.width});
  // Future footprints are predictions. None of these bits enter World.explored.
  std::vector<Bits> footprint(size);
  for(int cell=0;cell<size;++cell)if(!blocked[cell]) {
    const int px=cell%f.width,py=cell/f.width;
    for(int y=std::max(0,py-f.radius);y<=std::min(f.height-1,py+f.radius);++y)
      for(int x=std::max(0,px-f.radius);x<=std::min(f.width-1,px+f.radius);++x)
        if(unknown.test(y*f.width+x))footprint[cell].set(y*f.width+x);
  }
  // Empirical own-observation wall density estimates detours through blind
  // territory. A blocked attempt needs at least two extra moves to go around;
  // the geometric expected surcharge is 2*rho/(1-rho). This is a prior, not
  // an observation of any particular unknown cell.
  const double wall_fraction=double(w.obstacles.size())/std::max<size_t>(1,w.explored.size());
  const int penalty=std::lround(100*2*wall_fraction/std::max(.01,1-wall_fraction));
  std::vector<std::vector<int>> distances(size),parents(size);
  auto ensure=[&](int origin) {
    if(!distances[origin].empty())return;
    auto &dist=distances[origin];auto &parent=parents[origin];dist.assign(size,inf);parent.assign(size,-1);
    using Item=std::tuple<int,int,int>;
    std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;
    int serial=0;dist[origin]=0;queue.push({0,serial++,origin});
    while(!queue.empty()) {
      auto [cost,order,cell]=queue.top();queue.pop();(void)order;if(cost!=dist[cell])continue;
      Pos p{cell%f.width,cell/f.width};
      for(auto d:directions) {
        Pos q{p.x+d.x,p.y+d.y};if(!w.inside(q))continue;int next=q.y*f.width+q.x;
        if(blocked[next])continue;
        int edge=100+penalty*(int(unknown.test(cell))+int(unknown.test(next)))/2;
        int candidate=cost+edge;
        if(candidate<dist[next]){dist[next]=candidate;parent[next]=cell;queue.push({candidate,serial++,next});}
      }
    }
  };
  ensure(root);
  resources.erase(std::remove_if(resources.begin(),resources.end(),[&](int r){return distances[root][r]==inf;}),resources.end());
  auto distance=[&](int a,int b){ensure(a);return distances[a][b]>=inf?inf:(distances[a][b]+99)/100;};
  auto path=[&](int a,int b) {
    ensure(a);std::vector<int> result;
    if(distances[a][b]==inf)return result;
    for(int cell=b;cell!=a;cell=parents[a][cell])result.push_back(cell);
    std::reverse(result.begin(),result.end());return result;
  };
  // In a corridor with no new resources or walls, finish the selected leg.
  // Re-solving from a slightly different position/time caused two-step cycles.
  if(!stage1_joint_route_.empty() && stage1_joint_evaluation_.is_object() && stage1_joint_resources_==resource_points && stage1_joint_walls_==wall_points && stage1_joint_evaluation_.value("observed_cells",size_t(0))==w.explored.size()) {
    Pos goal=stage1_joint_route_.front();int cell=goal.y*f.width+goal.x;
    if(w.inside(goal) && goal!=f.pos && (value[cell]>0 || footprint[cell].any())) {
      auto planned=path(root,cell);
      if(!planned.empty()) {
        int first=planned.front();Pos next{first%f.width,first/f.width};
        target_=goal;target_type_=goal_reason=reason=value[cell]>0?"resource":"explore";
        stage1_route_evaluation=stage1_joint_evaluation_;
        stage1_route_evaluation["reused_leg"]=true;stage1_route_evaluation["elapsed_ms"]=elapsed();
        for(int d=0;d<4;++d)if(next==Pos{f.pos.x+directions[d].x,f.pos.y+directions[d].y})return Action{"move",names[d]};
      }
    }
  }
  auto length=[&](const std::vector<int> &route) {
    int cost=0,last=root;for(int node:route){cost+=distance(last,node);last=node;}return cost;
  };
  int nearest_resource=inf;for(int r:resources)nearest_resource=std::min(nearest_resource,distance(root,r));
  // Commit the first leg only when it is local to the current field of view.
  // A distant remembered item must compete with exploration rather than
  // forcing a long return across a corridor before any new observation.
  const bool protect_resource=nearest_resource<=2*f.radius;
  auto protected_front=[&](const std::vector<int> &route){return !route.empty() && value[route[0]]>0 && distance(root,route[0])<=2*f.radius;};
  auto improve=[&](std::vector<int> &route) {
    for(int pass=0;pass<6 && elapsed()<16;++pass) {
      bool changed=false;
      for(size_t i=protected_front(route)?1:0;i<route.size();++i)for(size_t j=i+1;j<route.size();++j) {
        int a=i?route[i-1]:root,b=route[i],c=route[j];
        int delta=distance(a,c)-distance(a,b);
        if(j+1<route.size())delta+=distance(b,route[j+1])-distance(c,route[j+1]);
        if(delta<0){std::reverse(route.begin()+i,route.begin()+j+1);changed=true;}
      }
      if(!changed)break;
    }
  };
  auto insert=[&](std::vector<int> &route,int node) {
    int best=inf,index=0;
    for(size_t i=0;i<=route.size();++i) {
      int before=i?route[i-1]:root,delta=distance(before,node);
      if(i<route.size())delta+=distance(route[i],node)-distance(before,route[i]);
      if(delta<best){best=delta;index=i;}
    }
    route.insert(route.begin()+index,node);
  };
  // An unknown resource first appears within the vision square. Its mean
  // Manhattan retrieval distance is 2*r*(r+1)/(2*r+1), a planning cost only.
  const double retrieval=2.0*f.radius*(f.radius+1)/(2*f.radius+1);
  // Extra retrieval is optional: seeing a hypothetical resource must not
  // consume time that is already reserved for real known resources.
  double unseen_count=unseen/12.5;
  if(std::abs(10*w.normal_collected+20*w.high_collected-f.resource_value)<1e-8) {
    int known_normal=0,known_high=0;
    for(const auto &[id,r]:w.resources){(void)id;if(r.value>=20)++known_high;else ++known_normal;}
    int normal=std::max(0,12-w.normal_collected-known_normal),high=std::max(0,4-w.high_collected-known_high);
    unseen_count=normal+high;
  }
  struct Estimate {double reward{},known{},ticks{};int length{};Bits covered;};
  auto evaluate=[&](const std::vector<int> &route) {
    Estimate best;Bits collected,covered;int last=root;double steps=0,earned=0;
    for(int node:route) {
      int previous=last;
      for(int cell:path(last,node)) {
        steps+=1+penalty*(int(unknown.test(previous))+int(unknown.test(cell)))/200.0;previous=cell;
        if(steps>budget)return best;
        if(value[cell]>0 && !collected.test(cell)){collected.set(cell);earned+=value[cell];}
        covered|=footprint[cell];double fraction=double(covered.count())/unknown_count;
        double mean=unseen*fraction;
        // Uniform-location mean, discounted by the available retrieval time.
        // This prediction is never an assertion about actual hidden cells.
        double reward=std::max(0.0,mean);
        double cost=unseen_count*fraction*retrieval;
        double taken=std::min(1.0,std::max(0.0,budget-steps)/std::max(.001,cost));
        double total=earned+reward*taken,ticks=steps+cost*taken;
        if(total>best.reward+1e-8 || (std::abs(total-best.reward)<1e-8 && (earned>best.known+1e-8
              || (std::abs(earned-best.known)<1e-8 && ticks<best.ticks))))
          best={total,earned,ticks,int(std::ceil(steps)),covered};
      }
      last=node;
    }
    return best;
  };
  auto coverage=[&](const std::vector<int> &route) {
    Bits covered;int last=root;
    for(int node:route){for(int cell:path(last,node))covered|=footprint[cell];last=node;}return covered;
  };
  std::vector<int> best_route;Estimate best;int variants=0;
  auto offer=[&](const std::vector<int> &route) {
    if(route.empty())return;
    auto estimate=evaluate(route);++variants;
    if(best_route.empty() || estimate.reward>best.reward+1e-8
        || (std::abs(estimate.reward-best.reward)<1e-8 && (estimate.known>best.known+1e-8
          || (std::abs(estimate.known-best.known)<1e-8 && estimate.ticks<best.ticks-1e-8)))) {
      best=estimate;best_route=route;
    }
  };
  std::vector<int> nearest,remaining=resources;int last=root;
  while(!remaining.empty()) {
    auto next=std::min_element(remaining.begin(),remaining.end(),[&](int a,int b){return distance(last,a)<distance(last,b);});
    last=*next;nearest.push_back(last);remaining.erase(next);
  }
  std::vector<int> warm;
  for(auto p:stage1_joint_route_) {
    if(!w.inside(p))continue;
    int cell=p.y*f.width+p.x;
    if(cell!=root && !blocked[cell] && distances[root][cell]<inf && (value[cell]>0 || footprint[cell].any())
        && std::find(warm.begin(),warm.end(),cell)==warm.end())warm.push_back(cell);
  }
  for(int resource:resources)if(std::find(warm.begin(),warm.end(),resource)==warm.end())insert(warm,resource);
  // Exact known-resource orienteering is an additional seed. Exploration
  // insertions must compete with a route that can bank actual observed value.
  std::vector<int> exact;int n=resources.size(),masks=1<<n;
  if(n) {
    std::vector<int> dp(masks*n,inf);std::vector<int8_t> parent(masks*n,-1);std::vector<double> rewards(masks);
    for(int mask=1;mask<masks;++mask){int bit=__builtin_ctz(static_cast<unsigned>(mask));rewards[mask]=rewards[mask^(1<<bit)]+value[resources[bit]];}
    for(int i=0;i<n;++i)dp[(1<<i)*n+i]=distance(root,resources[i]);
    double best_value=0;int best_mask=0,best_last=-1,best_cost=inf;
    for(int mask=1;mask<masks;++mask) {
      if((mask&255)==1 && elapsed()>8)break;
      for(int bits=mask;bits;bits&=bits-1) {
        int end=__builtin_ctz(static_cast<unsigned>(bits)),cost=dp[mask*n+end];if(cost>budget)continue;
        if(rewards[mask]>best_value+1e-8 || (std::abs(rewards[mask]-best_value)<1e-8 && cost<best_cost))
          {best_value=rewards[mask];best_cost=cost;best_mask=mask;best_last=end;}
        for(int rest=(masks-1)^mask;rest;rest&=rest-1) {
          int next=__builtin_ctz(static_cast<unsigned>(rest)),next_cost=cost+distance(resources[end],resources[next]);
          int index=(mask|(1<<next))*n+next;
          if(next_cost<=budget && next_cost<dp[index]){dp[index]=next_cost;parent[index]=end;}
        }
      }
    }
    int mask=best_mask,last=best_last;
    while(last>=0){exact.push_back(resources[last]);int previous=parent[mask*n+last];mask^=1<<last;last=previous;}
    std::reverse(exact.begin(),exact.end());
  }
  if(protect_resource) {
    auto first=std::find_if(warm.begin(),warm.end(),[&](int r){return value[r]>0 && distance(root,r)<=2*f.radius;});
    if(first!=warm.end())std::rotate(warm.begin(),first,first+1);
  }
  std::vector<std::vector<int>> seeds{exact,nearest,warm};
  if(!protect_resource)seeds.push_back({});
  for(auto route:seeds) {
    improve(route);offer(route);
    for(int view=0;view<8 && route.size()<24 && elapsed()<17;++view) {
      Bits uncovered=unknown&~coverage(route);if(uncovered.none())break;
      int selected=-1,insertion=0,best_cost=inf;double best_rate=-1;
      for(int cell=0;cell<size;++cell) {
        if(blocked[cell] || cell==root || distances[root][cell]==inf || std::find(route.begin(),route.end(),cell)!=route.end())continue;
        int gain=(footprint[cell]&uncovered).count();if(!gain)continue;
        for(size_t i=protected_front(route)?1:0;i<=route.size();++i) {
          int before=i?route[i-1]:root,cost=distance(before,cell);
          if(i<route.size())cost+=distance(route[i],cell)-distance(before,route[i]);
          double rate=double(gain)/(1+cost);
          if(rate>best_rate+1e-9 || (std::abs(rate-best_rate)<1e-9 && cost<best_cost)) {
            best_rate=rate;best_cost=cost;selected=cell;insertion=i;
          }
        }
      }
      if(selected<0)break;
      route.insert(route.begin()+insertion,selected);improve(route);offer(route);
      auto reverse=route;std::reverse(reverse.begin(),reverse.end());if(value[reverse.front()]>0 || !protect_resource)offer(reverse);
    }
    if(elapsed()>18)break;
  }
  if(best_route.empty() || best.reward<=1e-8)return std::nullopt;
  int goal=best_route.front();auto steps=path(root,goal);if(steps.empty())return std::nullopt;
  int first=steps.front();Pos next{first%f.width,first/f.width};
  if(!w.explored.count(next) || blocked[first])return std::nullopt;
  stage1_joint_route_.clear();Json route=Json::array();
  for(int cell:best_route){Pos p{cell%f.width,cell/f.width};stage1_joint_route_.push_back(p);route.push_back({p.x,p.y});}
  target_=Pos{goal%f.width,goal/f.width};target_type_=goal_reason=reason=value[goal]>0?"resource":"explore";
  stage1_route_evaluation={{"mode","joint_resource_coverage"},{"distance_model","optimistic_observed_walls"},
    {"route",route},{"modeled_route_cost",length(best_route)},{"protect_local_resource",protect_resource},{"nearest_resource_cost",nearest_resource},{"modeled_known_value",best.known},{"modeled_value",best.reward},{"modeled_ticks",best.ticks},
    {"observed_wall_fraction",wall_fraction},{"unknown_detour_cost",penalty/100.0},{"known_remaining_value",known},{"unseen_value",unseen},{"predicted_new_cells",best.covered.count()},
    {"unseen_prior","uniform_location_mean"},{"retrieval_distance_model",retrieval},{"variants",variants},{"elapsed_ms",elapsed()}};
  stage1_joint_resources_=resource_points;stage1_joint_walls_=wall_points;
  stage1_route_evaluation["observed_cells"]=w.explored.size();stage1_route_evaluation["model_tick"]=f.tick;stage1_joint_evaluation_=stage1_route_evaluation;
  for(int d=0;d<4;++d)if(next==Pos{f.pos.x+directions[d].x,f.pos.y+directions[d].y})return Action{"move",names[d]};
  return std::nullopt;
}
} // namespace rmtc
