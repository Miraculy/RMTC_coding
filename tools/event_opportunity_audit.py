"""Read one car's own event/decision history; never feeds a contestant.

Counts describe observed planning and occupancy, not counterfactual saved Ticks,
guaranteed buff acquisition, or the correctness of model-interpreted geometry.
Old logs lack goal_reason; their combat decisions can hide the selected goal.
"""
import argparse
import collections
import json
from pathlib import Path


def inside(position, rectangle):
    return (position is not None and rectangle is not None
            and rectangle[0][0] <= position[0] <= rectangle[1][0]
            and rectangle[0][1] <= position[1] <= rectangle[1][1])


def audit(directory, expected_ticks=500):
    records = [json.loads(line) for path in sorted(directory.glob('*.jsonl'))
               for line in path.read_text().splitlines()]
    sessions = {r['session_id'] for r in records if 'session_id' in r}
    if len(sessions) != 1:
        raise ValueError('Expected one car/session directory: ' + str(directory))
    frames = [r for r in records if r.get('kind') == 'decision']
    ai = [r for r in records if r.get('kind') == 'ai']
    known = []
    events = {}
    for record in records:
        if record.get('kind') == 'ai' and record.get('accepted'):
            for event in record.get('event_memory', {}).get('events', []):
                if event['kind'] != 'unrivaled':
                    continue
                key = json.dumps(event, sort_keys=True)
                if key not in events:
                    events[key] = dict(event=event, first_accepted_tick=record['current_tick'],
                                       lead_ticks=event['start_tick'] - record['current_tick'],
                                       counters=collections.Counter())
            if record.get('interpretation_applied', True):
                known = record.get('event_memory', {}).get('events', [])
        if record.get('kind') != 'decision' or not record.get('alive', record.get('hp', 0) > 0):
            continue
        for event in known:
            if event['kind'] != 'unrivaled' or record['tick'] >= event['end_tick']:
                continue
            counter = events[json.dumps(event, sort_keys=True)]['counters']
            rectangle = event.get('rect')
            prestart = record['tick'] < event['start_tick']
            selected = record.get('goal_reason', record.get('reason')) == 'fortress'
            if selected and inside(record.get('target'), rectangle):
                counter['prestart_goal_frames' if prestart else 'active_goal_frames'] += 1
            if inside(record['position'], rectangle):
                counter['prestart_inside_frames' if prestart else 'active_inside_frames'] += 1
                if (prestart and record['action']['action'] in ['wait', 'defend']
                        and record.get('target') == record['position']):
                    counter['prestart_stays_at_target'] += 1
                    if not record.get('observed_enemies'):
                        counter['prestart_stays_without_visible_enemy'] += 1
    return dict(directory=str(directory), sessions=len(sessions), decisions=len(frames),
                complete=[r['tick'] for r in frames] == list(range(expected_ticks)),
                explicit_goal_reason=bool(frames) and all('goal_reason' in r for r in frames),
                ai_results=len(ai), accepted_ai=sum(bool(r.get('accepted')) for r in ai),
                events=list(events.values()),
                final=None if not frames else {k: frames[-1].get(k) for k in
                    ['tick', 'resource_value', 'effective_damage', 'completed']})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    parser.add_argument('--expected-ticks', type=int, default=500)
    args = parser.parse_args()
    print(json.dumps([audit(p, args.expected_ticks) for p in args.directories],
                     ensure_ascii=False, indent=2))
