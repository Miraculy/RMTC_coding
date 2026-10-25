"""Strict no-event resource comparison, using each car's own logs separately.

Final public action receipts are read from saved practice diagnostics only by
this evaluator. No test data is sent back to a contestant.
"""
import argparse
import json
from pathlib import Path
import statistics

from compare_eval import compare, load_batches


def audit(paths):
    cases = load_batches(paths)
    counts = dict(matches=len(cases), decisions=0, receipts=0, successful_moves=0,
                  proposals=0, followed_proposals=0, race_discount_ticks=0, race_discount_records=0)
    details = []
    delta = {'up': (0, -1), 'down': (0, 1), 'left': (-1, 0), 'right': (1, 0)}
    for key, case in cases.items():
        if not case['run']['no_events']:
            raise ValueError('Direction audit requires no-event matches')
        if case['receipt_tick_mismatches'] or case['receipt_action_mismatches'] or case['missing_receipts']:
            raise ValueError('Unexplained action receipt mismatch: ' + str(key))
        row = case['row']; batch = Path(case['path'])
        label = row['variant'] + '-' + row['seat']
        state = json.loads((batch / (label + '-state.json')).read_text())
        for team, controller in row['controllers'].items():
            directory = batch / controller['log_dir']
            records = [r for p in directory.glob('*.jsonl')
                       for r in map(json.loads, p.read_text().splitlines())]
            frames = [r for r in records if r.get('kind') == 'decision']
            n = case['run']['max_ticks']
            if [r['tick'] for r in frames] != list(range(n)):
                raise ValueError('Incomplete car: ' + str(directory))
            final = state['checks'][team]
            if (final['perception_tick'] != n or final['executed_tick'] != n-1
                    or final['action_tick'] != n-1):
                raise ValueError('Missing final receipt: ' + str(directory))
            receipts = [(frames[t]['last_action'], frames[t-1], frames[t]['position']) for t in range(1, n)]
            receipts.append((final.get('last_action'), frames[-1], final['position']))
            for receipt, previous, position in receipts:
                if (not receipt or receipt.get('tick') != previous['tick']
                        or receipt.get('action') != previous['action']['action']):
                    raise ValueError('Wrong receipt: ' + str(directory))
                counts['receipts'] += 1
                if receipt.get('moved'):
                    direction = previous['action']['direction']; dx, dy = delta[direction]
                    if (receipt.get('effective_direction') != direction
                            or position != [previous['position'][0]+dx, previous['position'][1]+dy]):
                        raise ValueError('Wrong move direction: ' + str(directory))
                    counts['successful_moves'] += 1
            counts['decisions'] += len(frames)
            if team == row['seat']:
                proposals = [r for r in records if r.get('kind') == 'resource_route_evaluation']
                followed = sum(r['selected']['action'] == 'move'
                    and r['selected'].get('direction') == r['estimate']['proposed_first'] for r in proposals)
                counts['proposals'] += len(proposals); counts['followed_proposals'] += followed
                races = [r for r in records if r.get('kind') == 'resource_race_evaluation']
                counts['race_discount_ticks'] += len(races)
                counts['race_discount_records'] += sum(len(r['estimates']) for r in races)
                details.append(dict(case=list(key), resource_at={str(t): frames[t]['resource_value']
                    for t in (10, 20, 40, 60) if t < len(frames)}, proposals=len(proposals), followed=followed,
                    race_discount_ticks=len(races),
                    score=row['own']['score'], resources=row['own']['resource_value'], rank=row['rank']))
    return dict(**counts, details=details, resource_at_mean={str(t): statistics.mean(
        r['resource_at'][str(t)] for r in details) for t in (10, 20, 40, 60)})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', nargs='+', type=Path, required=True)
    parser.add_argument('--candidate', nargs='+', type=Path, required=True)
    args = parser.parse_args()
    result = dict(comparison=compare(args.control, args.candidate),
                  control=audit(args.control), candidate=audit(args.candidate))
    print(json.dumps(result, ensure_ascii=False, indent=2))
