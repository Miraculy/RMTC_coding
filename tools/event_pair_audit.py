"""Diagnose event-test confounders after complete practice matches.

Reads each car's own saved history separately; never feeds any contestant.
Earlier opponent divergence is a confounder flag, not proof of its cause.
Matching observed prefixes does not establish full causal equivalence either.
"""
import argparse
import json
from pathlib import Path

from compare_eval import compare, load_batches
from event_opportunity_audit import audit


def frames(directory):
    return [row for path in sorted(directory.glob('*.jsonl'))
            for row in map(json.loads, path.read_text().splitlines())
            if row.get('kind') == 'decision']


def clean(value):
    if isinstance(value, dict):
        return {k: clean(v) for k, v in value.items()
                if k not in ['session_id', 'wall_ms', 'latency_ms']}
    if isinstance(value, list):
        return [clean(v) for v in value]
    return value


def first_difference(left, right, keys):
    return next((a['tick'] for a, b in zip(left, right)
                 if any(clean(a.get(k)) != clean(b.get(k)) for k in keys)), None)


def inspect(controls, candidates):
    comparison = compare(controls, candidates)
    a, b = load_batches(controls), load_batches(candidates)
    cases = []
    # Inputs logged by a car, excluding its chosen plan and action. This does
    # not include every internal state variable or every model response.
    observation_keys = ['position', 'hp', 'alive', 'protected', 'initial_spawn',
                        'resource_value', 'effective_damage', 'completed',
                        'known_obstacles', 'observed_enemies', 'observed_resources',
                        'public_missions', 'last_action', 'map_size', 'vision_radius',
                        'known_resources', 'explored_cells']
    for key in sorted(a):
        old, new = a[key], b[key]
        if old['run']['opponents'] != 'self':
            raise ValueError('Requires four independently controlled cars')
        cars = {}
        for team in old['row']['controllers']:
            dirs = [Path(side['path']) / side['row']['controllers'][team]['log_dir']
                    for side in [old, new]]
            left, right = [frames(d) for d in dirs]
            action_tick = first_difference(left, right, ['action'])
            cars[team] = dict(
                first_action_difference=action_tick,
                first_goal_difference=first_difference(left, right, ['goal_reason', 'target']),
                first_logged_observation_difference=first_difference(left, right, observation_keys),
                first_action_context=None if action_tick is None else [
                    {k: clean(rows[action_tick].get(k)) for k in
                     ['tick', 'position', 'hp', 'resource_value', 'effective_damage',
                      'known_resources', 'goal_reason', 'target', 'action']}
                    for rows in [left, right]],
                event_opportunities=[audit(d, old['run']['max_ticks']) for d in dirs])
        focal = old['row']['seat']
        focal_tick = cars[focal]['first_action_difference']
        earlier = [team for team, car in cars.items() if team != focal
                   and car['first_action_difference'] is not None
                   and (focal_tick is None or car['first_action_difference'] < focal_tick)]
        cases.append(dict(case=list(key), focal=focal, cars=cars,
                          opponents_diverging_before_focal_action=earlier))
    return dict(
        note='Development-only. Earlier opponent actions flag confounding; absence does not prove causality. '
             'Accepted event structures can still be semantically wrong. No histories feed controllers.',
        comparison=comparison, cases=cases)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', nargs='+', type=Path, required=True)
    parser.add_argument('--candidate', nargs='+', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(inspect(args.control, args.candidate), ensure_ascii=False, indent=2))
