"""Offline audit of the failed-resource-entry experiment, never a bot input."""
import argparse
import json
from pathlib import Path

from compare_eval import compare, load_batches
from resource_pair_audit import audit


def first_differences(before, after):
    def normalize(value):
        if isinstance(value, dict):
            return {k: normalize(v) for k, v in value.items() if k not in ('session_id', 'latency_ms', 'wall_ms')}
        if isinstance(value, list):
            return [normalize(v) for v in value]
        return value

    result = []
    for key in sorted(before):
        differences = {}
        for team in before[key]['row']['controllers']:
            sides = []
            for case in (before[key], after[key]):
                directory = Path(case['path']) / case['row']['controllers'][team]['log_dir']
                sides.append([normalize(r) for p in sorted(directory.glob('*.jsonl'))
                              for r in map(json.loads, p.read_text().splitlines()) if r.get('kind') == 'decision'])
            differences[team] = dict(first_action=next((a['tick'] for a, b in zip(*sides) if a['action'] != b['action']), None),
                                     first_record=next((dict(tick=a['tick'], fields=[k for k in a if a[k] != b[k]])
                                                       for a, b in zip(*sides) if a != b), None))
        focal = before[key]['row']['seat']; t = differences[focal]['first_action']
        earlier_peers = [team for team, d in differences.items() if team != focal
                         and d['first_action'] is not None and (t is None or d['first_action'] < t)]
        result.append(dict(case=list(key), cars=differences, peers_changed_before_focal=earlier_peers))
    return result


def recovery_cases(paths):
    result = []
    for key, case in sorted(load_batches(paths).items()):
        row = case['row']
        directory = Path(case['path']) / row['controllers'][row['seat']]['log_dir']
        records = [r for p in sorted(directory.glob('*.jsonl'))
                   for r in map(json.loads, p.read_text().splitlines())]
        frames = {r['tick']: r for r in records if r.get('kind') == 'decision'}
        estimates = [r for r in records if r.get('kind') == 'resource_recovery_evaluation']
        starts = []
        for r in estimates:
            estimate = r['estimate']
            if estimate['status'] != 'started':
                continue
            t = r['tick']; p = estimate['alternative']
            resource = next(v for v in frames[t]['observed_resources'] if v['position'] == p)
            pickups = [n for n in range(t + 1, min(estimate['deadline'] + 1, len(frames)))
                       if frames[n]['position'] == p and frames[n]['alive']
                       and frames[n]['resource_value'] - frames[n - 1]['resource_value'] >= resource['value']]
            starts.append(dict(tick=t, estimate=estimate, action=r['selected'],
                               confirmed_pickup_ticks=pickups, resource_value=resource['value']))
        result.append(dict(case=list(key), starts=starts,
                           continued=sum(r['estimate']['status'] == 'continued' for r in estimates),
                           cancelled=sum(r['estimate']['status'] == 'cancelled' for r in estimates)))
    return result


def run(controls, candidates):
    before, after = load_batches(controls), load_batches(candidates)
    comparison = compare(controls, candidates)
    for key in before:
        x = dict(p.split(':=', 1) for p in before[key]['row']['planner_params'])
        y = dict(p.split(':=', 1) for p in after[key]['row']['planner_params'])
        if x.pop('resource_blocked_recovery', None) != 'false' or y.pop('resource_blocked_recovery', None) != 'true' or x != y:
            raise ValueError('Expected only the resource recovery parameter to differ: ' + str(key))
    return dict(comparison=comparison, control=audit(controls), candidate=audit(candidates),
                recovery=recovery_cases(candidates), first_differences=first_differences(before, after))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', nargs='+', type=Path, required=True)
    parser.add_argument('--candidate', nargs='+', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.control, args.candidate), ensure_ascii=False, indent=2))
