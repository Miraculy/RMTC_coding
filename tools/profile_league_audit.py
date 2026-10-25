"""Offline heterogeneous-cohort audit. Never provides observations to a car."""
import argparse
import collections
import json
from pathlib import Path
import statistics

from compare_eval import load_batches


def aggregate(rows):
    n = len(rows)
    counters = {}
    for field in ['actions', 'opening_reasons', 'completed_ids']:
        total = collections.Counter()
        for row in rows:
            total.update(row[field])
        counters[field] = dict(total)
    return dict(car_runs=n, seats=dict(collections.Counter(r['seat'] for r in rows)),
        mean_score=statistics.mean(r['score'] for r in rows),
        mean_resource=statistics.mean(r['resource'] for r in rows),
        mean_resource_points=statistics.mean(r['resource_points'] for r in rows),
        mean_combat_points=statistics.mean(r['combat_points'] for r in rows),
        mean_task_points=statistics.mean(r['task_points'] for r in rows),
        mean_damage=statistics.mean(r['damage'] for r in rows),
        mean_rank=statistics.mean(r['rank'] for r in rows),
        strict_first=sum(r['first'] for r in rows),
        all_three=sum(r['missions'] == 3 for r in rows),
        resource_cap=sum(r['resource'] >= 120 for r in rows),
        damage_cap=sum(r['damage'] >= 500 for r in rows),
        hp_zero_deaths=sum(r['hp_zero_deaths'] for r in rows),
        mean_resource_at_60=statistics.mean(r['resource_at_60'] for r in rows),
        mean_resource_at_100=statistics.mean(r['resource_at_100'] for r in rows), **counters)


def audit(paths):
    cases = load_batches(paths)
    if not cases:
        raise ValueError('No complete games')
    signatures = {json.dumps({k: c['run'].get(k) for k in
        ['profiles', 'lineup', 'lineup_schedule', 'tick_ms', 'max_ticks', 'no_events', 'ai',
         'judge_core_source_sha256', 'evaluator_sha256', 'evaluator_modules']}, sort_keys=True)
        for c in cases.values()}
    if len(signatures) != 1:
        raise ValueError('Do not mix lineups, focal versions, schedules or conditions in one league')
    profiles, focal_by_map, car_rows = collections.defaultdict(list), collections.defaultdict(list), []
    for key, case in sorted(cases.items()):
        row, run = case['row'], case['run']
        if not run.get('profiles'):
            raise ValueError('Expected heterogeneous profile cohort')
        for seat, controller in row['controllers'].items():
            records = [json.loads(line) for path in (Path(case['path']) / controller['log_dir']).glob('*.jsonl')
                       for line in path.read_text().splitlines()]
            frames = [r for r in records if r.get('kind') == 'decision']
            final = next((r for r in reversed(records) if r.get('kind') == 'final'), {})
            board = controller['own']
            car = dict(case=list(key), seat=seat, profile=controller['profile'],
                score=board['score'], resource=board['resource_value'], damage=board['effective_damage'],
                resource_points=30 * min(board['resource_value']/120, 1),
                combat_points=15 * min(board['effective_damage']/500, 1),
                task_points=5 * board['mission_completed'], missions=board['mission_completed'],
                rank=controller['rank'], first=controller['strict_first'],
                completed_ids=final.get('missions', frames[-1]['completed']),
                hp_zero_deaths=sum(a['hp'] > 0 and b['hp'] == 0 for a, b in zip(frames, frames[1:])),
                resource_at_60=frames[min(60,len(frames)-1)]['resource_value'],
                resource_at_100=frames[min(100,len(frames)-1)]['resource_value'],
                actions=dict(collections.Counter(r['action']['action'] for r in frames)),
                opening_reasons=dict(collections.Counter(r['reason'] for r in frames[:100])))
            if car['actions'] != controller['actions']:
                raise ValueError('Action summary mismatch')
            profiles[car['profile']].append(car)
            if seat == row['seat']:
                focal_by_map[row['variant']].append(car)
            car_rows.append(car)
    return dict(note='Four cars in one match are correlated. Same-map seat rotations are not independent maps. '
                    'Only each car own logs and audited final practice scores; no hidden-state input. '
                    'HP-zero deaths exclude immediate revival; no AI qualification claim.',
        games=len(cases), maps=len({c['row']['variant'] for c in cases.values()}),
        controlled_decisions=sum(c['car_decisions'] for c in cases.values()),
        receipt_tick_mismatches=sum(c['receipt_tick_mismatches'] for c in cases.values()),
        receipt_action_mismatches=sum(c['receipt_action_mismatches'] for c in cases.values()),
        missing_receipts=sum(c['missing_receipts'] for c in cases.values()),
        max_p95_ms=max(c['p95_ms'] for v in cases.values() for c in v['row']['controllers'].values()),
        max_ms=max(c['max_ms'] for v in cases.values() for c in v['row']['controllers'].values()),
        profiles={k:aggregate(v) for k,v in profiles.items()},
        focal_by_map={k:aggregate(v) for k,v in focal_by_map.items()}, cars=car_rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('batches', nargs='+', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.batches), ensure_ascii=False, indent=2))
