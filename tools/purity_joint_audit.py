"""Audit one car's purity estimates and public-model memory, after a match.

Does not read judge truth or another car's perceptions, and never sends data to
a controller. A heuristic branch probability is not an observed enemy action.
"""
import argparse
import collections
import json
from pathlib import Path


def audit(directory):
    rows = [json.loads(line) for path in sorted(directory.glob('*.jsonl'))
            for line in path.read_text().splitlines()]
    sessions = {r['session_id'] for r in rows if 'session_id' in r}
    if len(sessions) != 1:
        raise ValueError('Expected one car/session directory: ' + str(directory))
    decisions = [r for r in rows if r.get('kind') == 'decision']
    frames = {r['tick']: r for r in decisions}
    if len(frames) != len(decisions):
        raise ValueError('Duplicate decision Tick: ' + str(directory))
    ai = [r for r in rows if r.get('kind') == 'ai']
    versions = {}
    for row in ai:
        if not row.get('accepted'):
            continue
        for event in (row.get('event_memory') or {}).get('events', []):
            if event['kind'] == 'purity':
                key = json.dumps(event, sort_keys=True)
                versions.setdefault(key, dict(first_received_tick=row['current_tick'], event=event,
                    lead_ticks=event['start_tick'] - row['current_tick']))
    evaluations = [r for r in rows if r.get('kind') == 'purity_evaluation']
    corrected_certain = set()
    attacks = set()
    samples = []
    for row in evaluations:
        for estimate in row['candidates']:
            branches = estimate['branches_nondefend_then_defend']
            assert len(branches) == 2
            firing, defending = branches
            assert abs(sum(b['probability'] for b in branches) - 1) < 1e-9
            assert defending['attack'] == defending['kill_reward'] == 0
            assert abs(sum(b['probability'] * b['received'] for b in branches)
                       - estimate['expected_received']) < 1e-9
            assert abs(sum(b['probability'] for b in branches if b['dies'])
                       - estimate['death_probability']) < 1e-9
            hp = estimate['hp_after_environment']
            assert all(b['dies'] == (b['received'] >= hp) for b in branches)
            chosen = (row['selected']['action'] == 'attack'
                      and row['selected'].get('direction') == estimate['direction'])
            if chosen:
                attacks.add(row['tick'])
            old_received = firing['received'] + defending['probability'] * defending['received']
            false_certain = old_received >= hp and estimate['death_probability'] < 1
            if false_certain:
                corrected_certain.add(row['tick'])
            if chosen or false_certain:
                frame = frames[row['tick']]
                samples.append(dict(tick=row['tick'], position=frame['position'], hp=frame['hp'],
                    effective_damage=frame['effective_damage'], selected=row['selected'],
                    legacy_received=old_received, estimate=estimate))
    return dict(directory=str(directory), decisions=len(frames), ai_results=len(ai),
        http_statuses=dict(collections.Counter(str(r['http_status']) for r in ai)),
        http_success=sum(bool(r.get('http_success')) for r in ai),
        accepted=sum(bool(r.get('accepted')) for r in ai),
        schema_errors=dict(collections.Counter(r.get('schema_error') for r in ai if r.get('schema_error'))),
        purity_versions=list(versions.values()),
        evaluated_ticks=sorted({r['tick'] for r in evaluations}),
        chosen_attack_ticks=sorted(attacks), corrected_certain_death_ticks=sorted(corrected_certain),
        samples=samples)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    args = parser.parse_args()
    print(json.dumps([audit(d) for d in args.directories], ensure_ascii=False, indent=2))
