"""Find equal-cost information choices using a single car's own no-event log.

This is a one-frame counterfactual diagnostic, not a reconstructed judge map or
a promise of later resources. Never feeds another car's history to a contestant.
"""
import argparse
import heapq
import json
import math
from pathlib import Path

DIRS = [('up', (0, -1)), ('down', (0, 1)), ('left', (-1, 0)), ('right', (1, 0))]


def footprint(position, radius, width, height):
    return {(x, y) for x in range(max(0, position[0]-radius), min(width, position[0]+radius+1))
            for y in range(max(0, position[1]-radius), min(height, position[1]+radius+1))}


def choices(row, explored):
    width, height = row['map_size']; size = width*height
    origin = tuple(row['position']); root = origin[1]*width+origin[0]
    blocked = {tuple(p) for p in row['known_obstacles']}
    enemies = row['observed_enemies']; occupied = {tuple(e['position']) for e in enemies}
    def exposure(p):
        return sum(.6 for e in enemies if not e['protected']
                   and (p[0] == e['position'][0] or p[1] == e['position'][1])
                   and 0 < abs(p[0]-e['position'][0])+abs(p[1]-e['position'][1]) <= 2)
    cost = [math.inf]*size; steps = [0]*size; first = [-1]*size; masks = [0]*size
    cost[root] = 0; queue = [(0, root)]
    while queue:
        current, index = heapq.heappop(queue)
        if current > cost[index]+1e-6:
            continue
        p = index % width, index // width
        for d, (_, (dx, dy)) in enumerate(DIRS):
            q = p[0]+dx, p[1]+dy
            if not (0 <= q[0] < width and 0 <= q[1] < height) or q in blocked:
                continue
            if index == root and q in occupied:
                continue
            nxt = q[1]*width+q[0]
            new = current+1+(.35 if q not in explored else 0)+(1 if q in occupied else 0)+exposure(q)
            mask = 1 << d if index == root else masks[index]
            if new+1e-6 < cost[nxt]:
                cost[nxt] = new; steps[nxt] = steps[index]+1
                first[nxt] = d if index == root else first[index]; masks[nxt] = mask
                heapq.heappush(queue, (new, nxt))
            elif abs(new-cost[nxt]) < 1e-6 and steps[nxt] == steps[index]+1 and mask & ~masks[nxt]:
                masks[nxt] |= mask
                heapq.heappush(queue, (cost[nxt], nxt))
    target = row['target']; index = target[1]*width+target[0]; old = first[index]
    if old < 0:
        return None
    def next_position(d):
        return origin[0]+DIRS[d][1][0], origin[1]+DIRS[d][1][1]
    gain = [len(footprint(next_position(d), row['vision_radius'], width, height)-explored) for d in range(4)]
    candidates = [d for d in range(4) if masks[index] & (1 << d)
                  and exposure(next_position(d)) <= exposure(next_position(old))+1e-9]
    selected = max(candidates, key=lambda d: (gain[d], d == old))
    if gain[selected] <= gain[old]:
        return None
    return dict(tick=row['tick'], position=list(origin), target=target, steps=steps[index], cost=cost[index],
                baseline_first=DIRS[old][0], proposed_first=DIRS[selected][0],
                baseline_new_cells=gain[old], proposed_new_cells=gain[selected],
                actual_action=row['action']['action'], actual_direction=row['action'].get('direction'))


def audit(directory, opening_ticks=60):
    rows = [json.loads(line) for p in sorted(directory.glob('*.jsonl')) for line in p.read_text().splitlines()]
    if any(r.get('kind') == 'ai' and r.get('accepted') and
           (r.get('event_memory', {}).get('events') or r.get('event_memory', {}).get('effects')) for r in rows):
        raise ValueError('This diagnostic only reconstructs no-event paths')
    frames = [r for r in rows if r.get('kind') == 'decision']
    if not frames or len({r['session_id'] for r in frames}) != 1:
        raise ValueError('Expected one car/session directory')
    explored = set(); resources = {}; previous = None; high_collected = False; opportunities = []; windows = 0
    for row in frames:
        if row['tick'] >= opening_ticks:
            break
        if previous and row['tick'] == previous['tick']+1 and row['alive'] and previous['alive']:
            high_collected |= any(r['value'] >= 20 and r['position'] == row['position']
                                  and row['resource_value']-previous['resource_value'] >= r['value']
                                  for r in resources.values())
        previous = row
        if not row['alive']:
            continue
        width, height = row['map_size']; view = footprint(row['position'], row['vision_radius'], width, height)
        explored |= view
        visible = {r['id'] for r in row['observed_resources']}
        resources = {key: value for key, value in resources.items()
                     if tuple(value['position']) not in view or key in visible}
        resources.update({r['id']: r for r in row['observed_resources']})
        if high_collected or any(r['value'] >= 20 for r in resources.values()):
            continue
        # Older logs hide the selected goal when combat overrides reason; do
        # not invent it. Those windows are omitted from this diagnostic.
        if row.get('goal_reason', row.get('reason')) != 'resource' or not any(r['position'] == row['target'] for r in resources.values()):
            continue
        windows += 1
        alternative = choices(row, explored)
        if alternative:
            opportunities.append(alternative)
    return dict(directory=str(directory), explicit_goal_reason=all('goal_reason' in r for r in frames),
                eligible_frames=windows, opportunities=opportunities,
                zero_new_view_opportunities=sum(o['baseline_new_cells'] == 0 for o in opportunities),
                planned_move_opportunities=sum(o['actual_action'] == 'move' and
                    o['actual_direction'] == o['baseline_first'] for o in opportunities))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    args = parser.parse_args()
    print(json.dumps([audit(d) for d in args.directories], ensure_ascii=False, indent=2))
