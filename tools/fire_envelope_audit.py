#!/usr/bin/env python3
"""Own-log diagnostics for equally short steps outside one-step normal fire.

Only no-event cohorts with the alive/protected diagnostic fields are supported.
Unknown terrain is a path hypothesis; later own HP is observed evidence, not a
counterfactual result. Never read judge trajectories or other cars' private logs.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path


DIRECTIONS = {'up': (0, -1), 'right': (1, 0), 'down': (0, 1), 'left': (-1, 0)}


def aligned(a, b):
    return (a[0] == b[0] or a[1] == b[1]) and 0 < abs(a[0]-b[0])+abs(a[1]-b[1]) <= 2


def alternatives(frame, max_ticks):
    if ('alive' not in frame or 'protected' not in frame):
        raise ValueError('Requires the alive/protected own-log diagnostic schema')
    if (not frame['alive'] or frame['protected'] or frame['vision_radius'] < 4
            or frame['tick'] < max_ticks // 5 or frame['tick']+1 >= max_ticks
            or frame['action']['action'] != 'move'
            or frame['reason'] not in ['resource', 'explore', 'mission'] or frame['target'] is None):
        return None
    width, height = frame['map_size']
    pos, goal = tuple(frame['position']), tuple(frame['target'])
    walls = {tuple(p) for p in frame['known_obstacles']}
    robots = [r for r in frame['observed_enemies'] if r['hp'] > 0]
    occupied = {tuple(r['position']) for r in robots}
    incoming = 20 * sum(not r['protected'] and aligned(pos, r['position']) for r in robots)
    if not 0 < frame['hp']-incoming <= 40:
        return None
    if any(not r['protected'] and r['hp'] <= 20 and aligned(pos, r['position']) for r in robots):
        return None

    def inside(p):
        return 0 <= p[0] < width and 0 <= p[1] < height

    firing = []
    for robot in robots:
        p = tuple(robot['position'])
        firing.append(p)  # Protection may expire; current cells remain possible.
        for dx, dy in DIRECTIONS.values():
            q = p[0]+dx, p[1]+dy
            if inside(q) and q not in walls:
                firing.append(q)

    def exposed(p):
        return any(aligned(p, shot) for shot in firing)

    dx, dy = DIRECTIONS[frame['action']['direction']]
    selected = pos[0]+dx, pos[1]+dy
    if not inside(selected) or not exposed(selected) or goal in walls:
        return None
    distances, frontier = {goal: 0}, collections.deque([goal])
    while frontier:
        p = frontier.popleft()
        for dx, dy in DIRECTIONS.values():
            q = p[0]+dx, p[1]+dy
            if inside(q) and q not in walls and q not in distances:
                distances[q] = distances[p]+1
                frontier.append(q)
    if selected not in distances or pos not in distances:
        return None
    choices = []
    for label, (dx, dy) in DIRECTIONS.items():
        q = pos[0]+dx, pos[1]+dy
        length = distances.get(q)
        if (not inside(q) or q in walls or q in occupied or exposed(q) or length is None
                or length > distances[selected] or length+1 != distances[pos]
                or length+1 > max_ticks-frame['tick']):
            continue
        choices.append(dict(direction=label, position=list(q), estimated_steps_remaining=length))
    if choices:
        return dict(tick=frame['tick'], position=list(pos), hp=frame['hp'], current_fire_upper=incoming,
            target=list(goal), selected=frame['action']['direction'],
            selected_steps_remaining=distances[selected], alternatives=choices)
    return None


def audit(batches):
    cases, seen = [], set()
    for batch in batches:
        run = json.loads((batch / 'run.json').read_text())
        if not run['no_events']:
            raise ValueError('This diagnostic supports normal, no-event cohorts only')
        if hashlib.sha256((batch / 'contestant').read_bytes()).hexdigest() != run['binary_sha256']:
            raise ValueError('Frozen executable mismatch')
        for row in json.loads((batch / 'summary.json').read_text()):
            key = row['variant'], row['seat'], row.get('repetition', 1)
            if key in seen:
                raise ValueError('Duplicate variant/seat/repetition; inspect paired sides separately')
            seen.add(key)
            controller = row['controllers'][row['seat']]
            directory = batch / controller['log_dir']
            frames = [r for p in directory.glob('*.jsonl') for line in p.read_text().splitlines()
                      if (r := json.loads(line)).get('kind') == 'decision']
            if [r['tick'] for r in frames] != list(range(run['max_ticks'])):
                raise ValueError('Incomplete own control frames')
            windows = []
            for i, frame in enumerate(frames):
                result = alternatives(frame, run['max_ticks'])
                if result is not None:
                    result['observed_hp_two_ticks_later'] = frames[i+2]['hp'] if i+2 < len(frames) else None
                    windows.append(result)
            cases.append(dict(case=list(key), batch=str(batch), binary_sha256=run['binary_sha256'], windows=windows))
    return dict(note='Own views only. Fire envelopes are possibilities, not enemy intentions. '
                'Unknown terrain is assumed passable in estimated routes. Overlapping windows and later HP '
                'do not prove avoidable ticks, counterfactual survival, or final score gains.',
                cases=len(cases), windows=sum(len(c['windows']) for c in cases),
                affected_cases=sum(bool(c['windows']) for c in cases), details=cases)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('batches', type=Path, nargs='+')
    args = parser.parse_args()
    print(json.dumps(audit(args.batches), ensure_ascii=False, indent=2))
