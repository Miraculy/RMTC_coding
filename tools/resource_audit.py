"""Development-only resource diagnostics, from one car's legal records at a time.

Input CSVs and final boards are evaluation evidence, never contestant inputs.
An unchosen adjacent resource is an opportunity candidate, not a proven lost pickup.
"""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import statistics


def summarize_inputs(directory):
    tables = {}
    hashes = {}
    for name in ('matches', 'cars', 'batches'):
        path = directory / (name + '.csv')
        hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        with path.open(newline='', encoding='utf-8-sig') as stream:
            tables[name] = list(csv.DictReader(stream))
    matches, cars, batches = (tables[name] for name in ('matches', 'cars', 'batches'))
    sessions = [r['session_id'] for r in matches]
    if len(sessions) != len(set(sessions)):
        raise ValueError('Duplicate match session')
    counts = collections.Counter(r['session_id'] for r in cars)
    if set(counts) != set(sessions) or any(counts[s] != 4 for s in sessions):
        raise ValueError('Each match must join to four car records')
    teams = collections.defaultdict(set)
    for row in cars:
        teams[row['session_id']].add(row['team'])
    if any(team_set != set('ABCD') for team_set in teams.values()):
        raise ValueError('Each match must contain distinct teams A through D')
    v11 = [r for r in matches if r['damage_cap'] == '500' and r['complete'] == 'True']
    for r in v11:
        expected = .25 * min(120, float(r['resource_value'])) \
            + .03 * min(500, float(r['effective_damage'])) + 5 * float(r['mission_completed'])
        if abs(expected - float(r['score'])) > 1e-7:
            raise ValueError('Score formula mismatch: ' + r['session_id'])
    groups = []
    for batch in batches:
        if not batch['source'].startswith('experiments/'):
            continue
        rows = [r for r in v11 if r['batch'] == batch['batch']]
        if not rows:
            continue
        groups.append(dict(batch=batch['batch'], binary_sha256=batch['binary_sha256'],
            planner_params=batch['planner_params'], opponent_params=batch['opponent_params'],
            matches=len(rows), mean_resource=statistics.mean(float(r['resource_value']) for r in rows),
            mean_score=statistics.mean(float(r['score']) for r in rows)))
    return dict(note='Mixed versions/opponents/repeated fixtures: descriptive, not a policy win rate.',
        source_sha256=hashes, matches=len(matches), cars=len(cars), batches=len(batches),
        complete_v11=len(v11), mean_resource_v11=statistics.mean(float(r['resource_value']) for r in v11),
        mean_score_v11=statistics.mean(float(r['score']) for r in v11), experiment_groups=groups)


def audit_car(directory, opening_ticks=60):
    rows = [r for path in sorted(directory.glob('*.jsonl'))
            for r in map(json.loads, path.read_text().splitlines())]
    frames = [r for r in rows if r.get('kind') == 'decision']
    if not frames or len({r['session_id'] for r in frames}) != 1:
        raise ValueError('Expected one car/session: ' + str(directory))
    if [r['tick'] for r in frames] != list(range(len(frames))):
        raise ValueError('Incomplete/duplicate car frames: ' + str(directory))
    adjacent = []
    resources = {}
    blocked_entries = []
    for index, frame in enumerate(frames):
        observed = {r['id']: r for r in frame['observed_resources']}
        if index:
            previous = frames[index - 1]
            target = previous.get('target')
            if (previous.get('goal_reason') == 'resource' and target
                    and previous['action']['action'] == 'move'
                    and (frame.get('last_action') or {}).get('error') == 'E_BLOCKED'
                    and sum(abs(a-b) for a, b in zip(previous['position'], target)) == 1
                    and any(r['position'] == target for r in observed.values())):
                blocked_entries.append(dict(action_tick=previous['tick'], position=frame['position'],
                    resource=target, adjacent_enemies=[e for e in frame['observed_enemies']
                        if sum(abs(a-b) for a, b in zip(e['position'], target)) == 1]))
        for rid, resource in observed.items():
            resources.setdefault(rid, dict(id=rid, position=resource['position'], value=resource['value'],
                first_seen=frame['tick'], selected_ticks=[], visible_disappearances=[], observed_collections=[]))
        for rid, resource in resources.items():
            if frame.get('goal_reason') == 'resource' and frame.get('target') == resource['position']:
                resource['selected_ticks'].append(frame['tick'])
            if not index or not frame['alive']:
                continue
            previous = frames[index - 1]
            if not any(r['id'] == rid for r in previous['observed_resources']) or rid in observed:
                continue
            if max(abs(a-b) for a, b in zip(frame['position'], resource['position'])) > frame['vision_radius']:
                continue
            # A later own position and score increment can confirm a pickup.
            # Visible absence alone records disappearance, never who took it.
            if (frame['position'] == resource['position']
                    and abs(frame['resource_value'] - previous['resource_value'] - resource['value']) < 1e-7):
                resource['observed_collections'].append(frame['tick'])
            else:
                resource['visible_disappearances'].append(frame['tick'])
    for r in frames[:opening_ticks]:
        if not r['alive']:
            continue
        occupied = {tuple(e['position']) for e in r['observed_enemies']}
        incoming = sum(20 for e in r['observed_enemies'] if not e['protected']
            and (r['position'][0] == e['position'][0] or r['position'][1] == e['position'][1])
            and 0 < sum(abs(a-b) for a, b in zip(r['position'], e['position'])) <= 2)
        if r['protected']:
            incoming = 0
        for resource in r['observed_resources']:
            p = resource['position']
            if sum(abs(a-b) for a, b in zip(p, r['position'])) != 1 or tuple(p) in occupied:
                continue
            direction = ('right' if p[0] > r['position'][0] else 'left') if p[0] != r['position'][0] \
                else ('down' if p[1] > r['position'][1] else 'up')
            chosen = r['action']['action'] == 'move' and r['action'].get('direction') == direction
            if not chosen:
                adjacent.append(dict(tick=r['tick'], resource=resource, position=r['position'],
                    hp=r['hp'], conservative_normal_incoming=incoming,
                    target=r['target'], goal_reason=r.get('goal_reason'), action=r['action']))
    final = next((r for r in rows if r.get('kind') == 'final'), frames[-1])
    return dict(directory=str(directory), decisions=len(frames),
        resource_at={str(t): frames[t]['resource_value'] for t in (10, 20, 40, 60) if t < len(frames)},
        final_resource=final['resource_value'], adjacent_not_chosen=adjacent,
        resource_lifecycles=list(resources.values()),
        blocked_resource_entries=blocked_entries,
        nonlethal_adjacent_not_chosen=sum(r['hp'] > r['conservative_normal_incoming'] for r in adjacent))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--csv-dir', type=Path)
    parser.add_argument('--cars', nargs='*', type=Path, default=[])
    args = parser.parse_args()
    result = {}
    if args.csv_dir:
        result['input_summary'] = summarize_inputs(args.csv_dir)
    if args.cars:
        result['car_diagnostics'] = [audit_car(path) for path in args.cars]
    print(json.dumps(result, ensure_ascii=False, indent=2))
