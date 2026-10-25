"""Offline lifecycle of resources from ONE car's legal observations only.

Never reads judge configs, trajectories, or another car's logs. Missing unseen
resources have no fabricated IDs/positions. No-event diagnostic; unknown cells
in distance estimates are assumptions, never inserted into observed state.
"""
import argparse
import collections
import json
from pathlib import Path


def alive(frame):
    return frame.get('alive', frame['hp'] > 0)


def visible(frame, position):
    return alive(frame) and max(abs(a-b) for a,b in zip(frame['position'],position)) <= frame['vision_radius']


def distance_estimate(frame, target):
    width,height=frame['map_size'];blocked={tuple(p) for p in frame['known_obstacles']}
    origin=tuple(frame['position']);target=tuple(target)
    queue=collections.deque([(origin,0)]);seen={origin}
    while queue:
        point,length=queue.popleft()
        if point==target:return length
        for dx,dy in [(0,-1),(1,0),(0,1),(-1,0)]:
            q=point[0]+dx,point[1]+dy
            if 0<=q[0]<width and 0<=q[1]<height and q not in blocked and q not in seen:
                seen.add(q);queue.append((q,length+1))
    return None


def analyze(records, expected_ticks=500, opening_ticks=40):
    frames=[r for r in records if r.get('kind')=='decision']
    if [r['tick'] for r in frames]!=list(range(expected_ticks)):
        raise ValueError('Expected complete unique control ticks')
    if len({r['session_id'] for r in frames})!=1:
        raise ValueError('Mixed sessions are not a single car history')
    if any(r.get('kind')=='ai' and r.get('accepted') and
           (r.get('event_memory',{}).get('events') or r.get('event_memory',{}).get('effects')) for r in records):
        raise ValueError('This version reconstructs no-event observations only')
    resources={};previous=None;last_selected=None
    for frame in frames:
        if not alive(frame):
            observed={}
        else:
            observed={r['id']:r for r in frame['observed_resources']}
        for ident,resource in observed.items():
            if ident not in resources:
                resources[ident]=dict(id=ident,position=resource['position'],value=resource['value'],
                    first_seen_tick=frame['tick'],last_seen_tick=frame['tick'],
                    estimated_steps_at_discovery=distance_estimate(frame,resource['position']),
                    discovery_position=frame['position'],selected_ticks=[],interruptions=[],
                    passed_over_for_exploration=[],outcome='unresolved',resolution_tick=None,
                    visible_opponent_at_disappearance=[],reappeared_after_resolution=False)
            item=resources[ident]
            if item['position']!=resource['position'] or item['value']!=resource['value']:
                raise ValueError('Resource identity changed unexpectedly')
            if item['outcome']!='unresolved':item['reappeared_after_resolution']=True
            item['last_seen_tick']=frame['tick']
        # Own delta and landing position confirm collection; disappearance alone
        # never proves a capture, even if another robot happens to be nearby.
        for ident,item in resources.items():
            if item['outcome']!='unresolved':continue
            pos=item['position']
            delta=0 if previous is None else frame['resource_value']-previous['resource_value']
            if (previous is not None and alive(frame)
                    and pos==frame['position'] and delta>=item['value'] and ident not in observed
                    and visible(frame,pos)):
                item.update(outcome='own_collection_confirmed',resolution_tick=frame['tick'])
            elif ident not in observed and visible(frame,pos):
                item.update(outcome='visible_disappearance',resolution_tick=frame['tick'],
                    visible_opponent_at_disappearance=[e['team'] for e in frame['observed_enemies']
                        if e['hp']>0 and e['position']==pos])
        selected=next((ident for ident,item in resources.items()
            if item['outcome']=='unresolved' and frame.get('goal_reason',frame['reason'])=='resource'
            and frame.get('target')==item['position']),None)
        if last_selected is not None:
            item=resources[last_selected]
            reason=None
            if previous is not None and alive(previous) and not alive(frame):reason='observed_death'
            elif item['outcome']=='unresolved' and selected!=last_selected:reason='target_changed_while_unresolved'
            if reason is not None:
                item['interruptions'].append(dict(tick=frame['tick'],kind=reason,
                    action=frame['action']['action'],reason=frame['reason'],target=frame.get('target')))
        if selected is not None:
            item=resources[selected];item['selected_ticks'].append(frame['tick'])
            if frame['action']['action']!='move' and item['position']!=frame['position']:
                item['interruptions'].append(dict(tick=frame['tick'],kind='selected_but_not_moving',
                    action=frame['action']['action'],reason=frame['reason'],target=frame.get('target')))
        if frame['tick']<opening_ticks and frame.get('goal_reason',frame['reason'])=='explore':
            for item in resources.values():
                if item['outcome']!='unresolved':continue
                steps=distance_estimate(frame,item['position'])
                if steps is not None and steps<=5:
                    item['passed_over_for_exploration'].append(dict(tick=frame['tick'],
                        steps=steps,currently_visible=visible(frame,item['position']),
                        resource_value=item['value'],explore_target=frame.get('target'),
                        action=frame['action']['action']))
        last_selected=selected;previous=frame
    public=frames[-1].get('public_missions',[])
    final=next((r for r in reversed(records) if r.get('kind')=='final'),{})
    completed=final.get('missions',frames[-1]['completed'])
    unseen_high_gap=(not any(r['value']>=20 for r in resources.values()) and
        any(m['type']=='high_resources' and m['id'] not in completed for m in public))
    rows=list(resources.values())
    return dict(session_id=frames[0]['session_id'],frames=len(frames),
        resource_by_tick={str(t):frames[t]['resource_value'] for t in [10,20,40] if t<len(frames)},
        own_resource_at_last_decision=frames[-1]['resource_value'],
        own_resource_final_observed=final.get('resource_value',frames[-1]['resource_value']),
        completed_missions=completed,unseen_high_task_information_gap=unseen_high_gap,
        outcomes=dict(collections.Counter(r['outcome'] for r in rows)),
        discovered=len(rows),selected=sum(bool(r['selected_ticks']) for r in rows),
        resources=rows,note='Own legal history only. Passed-over resources are candidates, not proven capturable losses. '
            'Distance uses only remembered static walls with unknown cells assumed walkable; no exact future enemy positions. '
            'Visible disappearance is not proof of which robot collected. Unseen resources remain unknown.')


def audit(directory,expected_ticks=500,opening_ticks=40):
    records=[json.loads(line) for p in sorted(directory.glob('*.jsonl')) for line in p.read_text().splitlines()]
    return dict(directory=str(directory),**analyze(records,expected_ticks,opening_ticks))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories',nargs='+',type=Path)
    parser.add_argument('--expected-ticks',type=int,default=500)
    parser.add_argument('--opening-ticks',type=int,default=40)
    args=parser.parse_args()
    print(json.dumps([audit(d,args.expected_ticks,args.opening_ticks) for d in args.directories],ensure_ascii=False,indent=2))
