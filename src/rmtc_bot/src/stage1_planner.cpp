#include "rmtc_bot/core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>

namespace rmtc {
std::optional<Action> Planner::decide_stage1(const World &w) {
  using Clock=std::chrono::steady_clock;const auto begin=Clock::now();const auto &f=w.current;
  const int size=f.width*f.height,remaining=max_ticks_-f.tick,root=f.pos.y*f.width+f.pos.x,inf=1000000;
  if(size>4096 || w.resources.size()>16 || !w.events.items.empty() || !w.events.effects.empty() || !w.enemies.empty())return std::nullopt;
  if(stage1_cache_session_!=f.session)stage1_certified_incumbent_.clear();
  stage1_certified_incumbent_.erase(std::remove_if(stage1_certified_incumbent_.begin(),stage1_certified_incumbent_.end(),[&](Pos p) {
    return std::none_of(w.resources.begin(),w.resources.end(),[&](const auto &entry){return entry.second.pos==p;});
  }),stage1_certified_incumbent_.end());
  constexpr std::array<Pos,4> directions{{{0,-1},{1,0},{0,1},{-1,0}}};
  constexpr std::array<const char*,4> names{{"up","right","down","left"}};
  auto elapsed=[&](){return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();};
  std::vector<Resource> resources;for(const auto &[id,r]:w.resources){(void)id;resources.push_back(r);}
  std::sort(resources.begin(),resources.end(),[](const auto &a,const auto &b){return a.pos.y!=b.pos.y?a.pos.y<b.pos.y:a.pos.x<b.pos.x;});
  const int n=resources.size(),count=1<<n;double known=0;
  std::vector<Pos> points,walls;for(const auto &r:resources){points.push_back(r.pos);known+=r.value;}
  for(const auto &r:resources)if(r.pos==f.pos) {
    target_=f.pos;target_type_=goal_reason=reason="resource";
    stage1_route_evaluation={{"mode","collect_underfoot"},{"planned_value",r.value},{"planned_steps",1},{"solver_complete",true}};
    return Action{};
  }
  std::vector<bool> blocked(size);
  for(int i=0;i<size;++i)if(w.blocked({i%f.width,i/f.width},f.tick) || w.occupied({i%f.width,i/f.width})) {
    blocked[i]=true;walls.push_back({i%f.width,i/f.width});
  }
  auto bfs=[&](Pos origin) {
    std::vector<int> dist(size,inf),queue(size);int read=0,write=0,first=origin.y*f.width+origin.x;
    dist[first]=0;queue[write++]=first;
    while(read<write) {
      int i=queue[read++];Pos p{i%f.width,i/f.width};
      for(auto d:directions) {
        Pos q{p.x+d.x,p.y+d.y};if(!w.inside(q))continue;int j=q.y*f.width+q.x;
        if(blocked[j] || dist[j]!=inf)continue;
        dist[j]=dist[i]+1;queue[write++]=j;
      }
    }
    return dist;
  };
  auto lower_origin=bfs(f.pos);
  // Standard public total, minus authoritative own score and observed inventory.
  double unseen=std::max(0.0,200-f.resource_value-known);bool reachable_unknown=false;
  for(int i=0;i<size;++i)if(!w.explored.count({i%f.width,i/f.width}) && lower_origin[i]<=remaining){reachable_unknown=true;break;}
  double uncertainty=reachable_unknown?unseen:0;
  if(stage1_cache_session_!=f.session || stage1_cache_width_!=f.width || stage1_cache_height_!=f.height
      || stage1_cache_points_!=points || stage1_cache_walls_!=walls) {
    stage1_cache_distances_.clear();for(auto p:points)stage1_cache_distances_.push_back(bfs(p));
    stage1_cache_session_=f.session;stage1_cache_width_=f.width;stage1_cache_height_=f.height;
    stage1_cache_points_=points;stage1_cache_walls_=walls;
  }
  // The executable plan only uses cells actually observed free. The relaxed
  // distances above provide an upper bound on what any real route could earn.
  for(int i=0;i<size;++i)if(!w.explored.count({i%f.width,i/f.width}))blocked[i]=true;
  auto safe_origin=bfs(f.pos);std::vector<std::vector<int>> safe_dist;
  for(auto p:points)safe_dist.push_back(bfs(p));
  struct Solution {double value{};int length{};std::vector<int> order;bool complete{true};};
  std::vector<double> values(count);
  for(int mask=1;mask<count;++mask){int bit=__builtin_ctz(static_cast<unsigned>(mask));values[mask]=values[mask^(1<<bit)]+resources[bit].value;}
  auto solve=[&](const std::vector<int> &origin,const std::vector<std::vector<int>> &dm) {
    Solution best;int best_mask=0,best_last=-1;std::vector<int> dp(count*n,inf);std::vector<int8_t> parent(count*n,-1);
    auto d=[&](int previous,int next){return previous<0?origin[points[next].y*f.width+points[next].x]:dm[previous][points[next].y*f.width+points[next].x];};
    std::vector<int> seed;std::vector<bool> used(n);int last=-1;
    for(int k=0;k<n;++k) {
      int selected=-1,dist=inf;for(int i=0;i<n;++i)if(!used[i] && d(last,i)<dist){dist=d(last,i);selected=i;}
      if(selected<0)break;
      used[selected]=true;seed.push_back(selected);last=selected;
    }
    auto length=[&](const std::vector<int> &order){int total=0,previous=-1;for(int i:order){total+=d(previous,i);previous=i;}return total;};
    for(int pass=0;pass<6;++pass) {
      bool improved=false;int cost=length(seed);
      for(size_t i=0;i<seed.size();++i)for(size_t j=i+1;j<seed.size();++j) {
        auto trial=seed;std::reverse(trial.begin()+i,trial.begin()+j+1);int candidate=length(trial);
        if(candidate<cost){seed=trial;cost=candidate;improved=true;}
      }
      if(!improved)break;
    }
    int cost=0;double value=0;last=-1;
    for(int i:seed){int next=cost+d(last,i);if(next>remaining)break;cost=next;value+=resources[i].value;best.order.push_back(i);last=i;}
    best.value=value;best.length=cost;
    for(int i=0;i<n;++i)if(d(-1,i)<=remaining)dp[(1<<i)*n+i]=d(-1,i);
    for(int mask=1;mask<count;++mask) {
      if((mask&255)==1 && elapsed()>20){best.complete=false;break;}
      for(int bits=mask;bits;bits&=bits-1) {
        int end=__builtin_ctz(static_cast<unsigned>(bits)),dist=dp[mask*n+end];if(dist>remaining)continue;
        if(values[mask]>best.value+1e-9 || (std::abs(values[mask]-best.value)<1e-9 && (dist<best.length || (dist==best.length && best_last<0)))) {
          best.value=values[mask];best.length=dist;best_mask=mask;best_last=end;
        }
        for(int rest=(count-1)^mask;rest;rest&=rest-1) {
          int next=__builtin_ctz(static_cast<unsigned>(rest)),candidate=dist+d(end,next),index=(mask|(1<<next))*n+next;
          if(candidate<=remaining && candidate<dp[index]){dp[index]=candidate;parent[index]=static_cast<int8_t>(end);}
        }
      }
    }
    if(best_last>=0){best.order.clear();int mask=best_mask;last=best_last;
      while(last>=0){best.order.push_back(last);int previous=parent[mask*n+last];mask^=1<<last;last=previous;}
      std::reverse(best.order.begin(),best.order.end());}
    return best;
  };
  auto safe=solve(safe_origin,safe_dist);
  const double upper_bound=known;
  // Forecast only the old controller's prefix whose complete next footprints
  // are already observed. Stop before any new observation would be required.
  // No hypothetical cell is inserted into the real or forecast World.
  Pos forecast_pos=f.pos;int forecast_budget=remaining,collected_mask=0,prefix_ticks=0;
  double prefix_value=0;
  if(stage1_certified_incumbent_.empty()) {
    Planner baseline=*this;baseline.config_.stage1_resource_route=false;World predicted=w;
    while(forecast_budget>0 && elapsed()<10) {
      auto action=baseline.decide(predicted);Pos next=forecast_pos;
      if(action.type=="move") {
        for(int d=0;d<4;++d)if(action.direction==names[d]){next.x+=directions[d].x;next.y+=directions[d].y;}
      }
      if(!w.inside(next) || blocked[next.y*f.width+next.x])break;
      for(int i=0;i<n;++i)if(!(collected_mask&(1<<i)) && points[i]==next) {
        collected_mask|=1<<i;prefix_value+=resources[i].value;
      }
      --forecast_budget;++prefix_ticks;forecast_pos=next;
      bool fully_known=true;
      for(int y=std::max(0,next.y-f.radius);y<=std::min(f.height-1,next.y+f.radius);++y)
        for(int x=std::max(0,next.x-f.radius);x<=std::min(f.width-1,next.x+f.radius);++x)
          if(!w.explored.count({x,y}))fully_known=false;
      if(!fully_known || forecast_budget==0)break;
      Frame frame=predicted.current;++frame.tick;frame.pos=next;frame.resource_value=f.resource_value+prefix_value;
      frame.moved=action.type=="move";frame.entities.clear();
      auto visible=[&](Pos p){return std::abs(p.x-next.x)<=f.radius && std::abs(p.y-next.y)<=f.radius;};
      for(auto p:walls)if(visible(p))frame.entities.push_back({"obstacle","","",p,100,0,false,false});
      for(int i=0;i<n;++i)if(!(collected_mask&(1<<i)) && visible(points[i]))
        frame.entities.push_back({resources[i].value>=20?"resource_high":"resource_normal",std::to_string(i),"",points[i],100,resources[i].value,false,false});
      // Preserve resource IDs: they are protocol identity, not position clues.
      for(auto &entity:frame.entities)if(entity.type!="obstacle")
        for(const auto &[id,r]:w.resources)if(r.pos==entity.pos){entity.id=id;break;}
      predicted.update(frame);
    }
  }
  // Relax the entire unseen inventory into one virtual node. Its incoming and
  // outgoing legs may use different nearest unknown cells, so this is a lower
  // distance bound and an upper value bound on every actual continuation.
  auto relaxed_bound=[&](Pos origin,int budget,int excluded) {
    if(budget<=0)return std::pair<double,bool>{0,true};
    auto origin_dist=bfs(origin); // Restore optimistic rather than safe mask.
    double bound=0;int allowed=(count-1)^excluded;
    std::vector<int> to_unknown(n+1,inf);
    for(int cell=0;cell<size;++cell)if(!w.explored.count({cell%f.width,cell/f.width})) {
      to_unknown[n]=std::min(to_unknown[n],origin_dist[cell]);
      for(int i=0;i<n;++i)to_unknown[i]=std::min(to_unknown[i],stage1_cache_distances_[i][cell]);
    }
    std::vector<int> before(count*n,inf),after(count*n,inf),ghost(count,inf);ghost[0]=to_unknown[n];
    for(int i=0;i<n;++i)if(allowed&(1<<i))before[(1<<i)*n+i]=origin_dist[points[i].y*f.width+points[i].x];
    for(int mask=0;mask<count;++mask) {
      if(mask&excluded)continue;
      if((mask&255)==0 && elapsed()>20)return std::pair<double,bool>{values[allowed]+unseen,false};
      for(int bits=mask;bits;bits&=bits-1) {
        int end=__builtin_ctz(static_cast<unsigned>(bits));
        if(before[mask*n+end]<=budget)bound=std::max(bound,values[mask]);
        ghost[mask]=std::min(ghost[mask],before[mask*n+end]+to_unknown[end]);
      }
      if(ghost[mask]<=budget)bound=std::max(bound,values[mask]+unseen);
      for(int rest=allowed^mask;rest;rest&=rest-1) {
        int next=__builtin_ctz(static_cast<unsigned>(rest)),index=(mask|(1<<next))*n+next;
        after[index]=std::min(after[index],ghost[mask]+to_unknown[next]);
      }
      for(int bits=mask;bits;bits&=bits-1) {
        int end=__builtin_ctz(static_cast<unsigned>(bits));
        if(after[mask*n+end]<=budget)bound=std::max(bound,values[mask]+unseen);
        for(int rest=allowed^mask;rest;rest&=rest-1) {
          int next=__builtin_ctz(static_cast<unsigned>(rest)),index=(mask|(1<<next))*n+next;
          int d=stage1_cache_distances_[end][points[next].y*f.width+points[next].x];
          before[index]=std::min(before[index],before[mask*n+end]+d);
          after[index]=std::min(after[index],after[mask*n+end]+d);
        }
      }
    }
    return std::pair<double,bool>{bound,true};
  };
  // BFS for the value bound must allow unobserved cells, without treating
  // them as observed or allowing them into the executable route.
  auto safe_blocked=blocked;for(int i=0;i<size;++i)if(!w.explored.count({i%f.width,i/f.width}))blocked[i]=false;
  auto forecast_bound=relaxed_bound(forecast_pos,forecast_budget,collected_mask);
  blocked=std::move(safe_blocked);
  double total_upper=prefix_value+forecast_bound.first;
  bool unseen_complete=forecast_bound.second;
  bool certified=safe.value>=total_upper-10-1e-9;
  auto position_index=[&](Pos p){auto it=std::find(points.begin(),points.end(),p);return it==points.end()?-1:int(it-points.begin());};
  std::vector<Pos> incumbent;double incumbent_value=0;int incumbent_length=0,last=-1;
  for(auto p:stage1_certified_incumbent_) {
    int i=position_index(p);if(i<0)continue;
    incumbent_length+=last<0?safe_origin[p.y*f.width+p.x]:safe_dist[last][p.y*f.width+p.x];
    incumbent_value+=resources[i].value;incumbent.push_back(p);last=i;
  }
  bool lost=incumbent_length>remaining;
  if((certified || !incumbent.empty()) && (safe.value>=incumbent_value-1e-9 || lost)) {
    incumbent.clear();for(int i:safe.order)incumbent.push_back(points[i]);
    incumbent_value=safe.value;incumbent_length=safe.length;
  }
  stage1_route_evaluation={{"mode",certified?"certified_resource_route":"retained_certified_route"},
    {"planned_value",incumbent_value},{"planned_steps",incumbent_length},{"unseen_value_upper_bound",uncertainty},
    {"baseline_known_prefix_ticks",prefix_ticks},{"baseline_known_prefix_value",prefix_value},{"known_value_lower_bound",safe.value},{"known_value_upper_bound",upper_bound},{"baseline_value_upper_bound",total_upper},
    {"solver_complete",safe.complete && unseen_complete},{"value_certified",certified},{"incumbent_interrupted",lost},
    {"distance_model","observed_free_route_with_optimistic_value_bound"}};
  if(incumbent.empty()) {
    if(!certified)return std::nullopt;
    if(reachable_unknown && unseen>0)return std::nullopt;
    target_.reset();target_type_.clear();goal_reason.clear();reason="wait";
    stage1_route_evaluation["mode"]="no_reachable_gain";return Action{};
  }
  stage1_certified_incumbent_=incumbent;Pos goal=incumbent.front();int first=position_index(goal);
  target_=goal;target_type_="stage1_certified_resource";goal_reason=reason="resource";
  Json route=Json::array();for(auto p:incumbent)route.push_back({p.x,p.y});stage1_route_evaluation["route"]=route;
  auto &toward=safe_dist[first];int selected=-1,gain=-1,visits=inf;
  for(int d=0;d<4;++d) {
    Pos q{f.pos.x+directions[d].x,f.pos.y+directions[d].y};if(!w.inside(q))continue;int i=q.y*f.width+q.x;
    if(blocked[i] || toward[i]+1!=toward[root])continue;
    int novel=0;for(int y=std::max(0,q.y-f.radius);y<=std::min(f.height-1,q.y+f.radius);++y)
      for(int x=std::max(0,q.x-f.radius);x<=std::min(f.width-1,q.x+f.radius);++x)if(!w.explored.count({x,y}))++novel;
    auto visit=w.visits.find(q);int count=visit==w.visits.end()?0:visit->second;
    if(novel>gain || (novel==gain && count<visits)){gain=novel;visits=count;selected=d;}
  }
  stage1_route_evaluation["elapsed_ms"]=elapsed();
  if(selected<0)return std::nullopt;
  return Action{"move",names[selected]};
}
} // namespace rmtc
