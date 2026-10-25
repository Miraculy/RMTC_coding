"""Compare complete, frozen practice cohorts; never used by a contestant.

Reads deployment files only to verify equal test fixtures. Each car's control
coverage is audited separately; private observations are never pooled into a
strategy. Reported task points remain practice results, not formal qualification.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import statistics

from strategy_profiles import case_key, arrangements


def read_json(path):
    return json.loads(path.read_text())


def rank_key(team):
    return (round(team['score'], 9), team['mission_completed'], round(team['resource_value'], 9),
            round(team['effective_damage'], 9), -team.get('invalid_actions', 0))


def load_batches(paths):
    cases = {}
    for path in paths:
        run = read_json(path / 'run.json')
        if hashlib.sha256((path / 'contestant').read_bytes()).hexdigest() != run['binary_sha256']:
            raise ValueError(f'Frozen binary mismatch: {path}')
        source = path / 'strategy_eval.py'
        if source.exists() and hashlib.sha256(source.read_bytes()).hexdigest() != run['evaluator_sha256']:
            raise ValueError(f'Evaluator snapshot mismatch: {path}')
        for name, digest in run.get('evaluator_modules', {}).items():
            if hashlib.sha256((path / name).read_bytes()).hexdigest() != digest:
                raise ValueError(f'Evaluator dependency mismatch: {path}, {name}')
        profiles = run.get('profiles')
        if profiles:
            manifest = read_json(path / 'lineup.json')
            if manifest['profiles'] != profiles or manifest['lineup'] != run['lineup']:
                raise ValueError('Profile manifest mismatch')
            for name, spec in profiles.items():
                if hashlib.sha256((path / spec['frozen_binary']).read_bytes()).hexdigest() != spec['binary_sha256']:
                    raise ValueError(f'Frozen profile mismatch: {name}')
        schedules = dict(arrangements(run['lineup'], run['lineup_schedule'], run['arrangements'])) if profiles else {}
        rows = read_json(path / 'summary.json')
        if profiles:
            expected_keys = {(variant, next(t for t,n in a.items() if n == run['lineup'][0]), repetition, i)
                for variant in run['variants'].split(',') for i,a in schedules.items()
                for repetition in range(1, run['repetitions'] + 1)}
            if {case_key(r) for r in rows} != expected_keys or len(rows) != len(expected_keys):
                raise ValueError('Incomplete planned profile batch')
        for row in rows:
            key = case_key(row)
            if key in cases:
                raise ValueError(f'Duplicate scenario/seat/repetition: {key}')
            if not profiles and row['binary_sha256'] != run['binary_sha256']:
                raise ValueError(f'Mixed binary within a batch: {path}')
            controllers = row.get('controllers') or {row['seat']: row}
            if profiles:
                if schedules.get(row['arrangement']) != row['assignments']:
                    raise ValueError('Arrangement does not match frozen schedule')
                if set(controllers) != set('ABCD') or set(row['assignments']) != set('ABCD'):
                    raise ValueError('Incomplete profile assignment')
                if sorted(row['assignments'].values()) != sorted(run['lineup']):
                    raise ValueError('Wrong profile lineup')
                if row['focal_profile'] != row['assignments'][row['seat']] or row['focal_profile'] != run['lineup'][0]:
                    raise ValueError('Wrong focal profile')
                focal = profiles[row['focal_profile']]
                if row['binary_sha256'] != focal['binary_sha256'] or row['planner_params'] != focal['params']:
                    raise ValueError('Focal executable/parameters mismatch')
                parameters = read_json(path / (row['case_label'] + '-parameters.json'))
                for team, controller in controllers.items():
                    name = row['assignments'][team]; spec = profiles[name]
                    if (controller.get('profile') != name or controller.get('binary_sha256') != spec['binary_sha256']
                            or controller['planner_params'] != spec['params'] or parameters[team]['profile'] != name):
                        raise ValueError('Controller profile mismatch')
                    expected = {p.split(':=', 1)[0]: json.loads(p.split(':=', 1)[1]) for p in spec['params']}
                    actual = parameters[team]['declared_parameters']
                    if parameters[team]['checked_parameters'] != expected or actual.get('team') != team or actual.get('stage') != 2:
                        raise ValueError('Runtime parameter audit mismatch')
                    if any(type(actual.get(k)) is not type(v) or actual.get(k) != v for k, v in expected.items()):
                        raise ValueError('Runtime profile parameters differ')
            receipt_mismatches = receipt_action_mismatches = missing_receipts = 0
            for team, controller in controllers.items():
                board = next(t for t in row['teams'] if t['team'] == team)
                expected = 30 * min(board['resource_value'] / 120, 1) \
                    + 15 * min(board['effective_damage'] / run.get('damage_cap', 300), 1) \
                    + 5 * board['mission_completed']
                if abs(board['score'] - expected) > 1e-7 or controller['own'] != board:
                    raise ValueError(f'Inconsistent practice score: {path}, {team}')
                rank = 1 + sum(rank_key(t) > rank_key(board) for t in row['teams'])
                first = all(rank_key(board) > rank_key(t) for t in row['teams'] if t['team'] != team)
                if controller['rank'] != rank or controller['strict_first'] != first:
                    raise ValueError(f'Inconsistent ranking: {path}, {team}')
                directory = path / controller.get('log_dir', row['variant'] + '-' + row['seat'])
                if 'log_dir' not in controller and row.get('repetition', 1) > 1:
                    directory = path / (row['variant'] + '-' + row['seat'] + '-r' + str(row['repetition']))
                records = [json.loads(line) for p in directory.glob('*.jsonl') for line in p.read_text().splitlines()]
                frames = [r for r in records if r.get('kind') == 'decision']
                if [r['tick'] for r in frames] != list(range(run['max_ticks'])):
                    raise ValueError(f'Incomplete control frames: {directory}')
                if (not controller['frames_complete'] or controller['decisions'] != run['max_ticks']
                        or controller['own'].get('invalid_actions', 0) or controller['rejected_frames']):
                    raise ValueError(f'Invalid controlled car: {directory}')
                receipt_mismatches += sum(bool(r.get('last_action'))
                    and r['last_action'].get('tick') != r['tick'] - 1 for r in frames if r['tick'] > 0)
                submitted = collections.defaultdict(set)
                for record in records:
                    if record.get('kind') in ['decision', 'action_revision']:
                        submitted[record['tick']].add(record['action']['action'])
                for frame in frames:
                    if frame['tick'] == 0:
                        continue
                    receipt = frame.get('last_action')
                    if not receipt:
                        missing_receipts += 1
                    elif receipt.get('action') not in submitted[receipt.get('tick')]:
                        # Report rather than reject: a rule-forced WAIT may
                        # legitimately differ from any submitted action.
                        receipt_action_mismatches += 1
                if run.get('opponents') == 'self':
                    label = row.get('case_label') or row['variant'] + '-' + row['seat']
                    if 'case_label' not in row and row.get('repetition', 1) > 1:
                        label += '-r' + str(row['repetition'])
                    interface = read_json(path / (label + '-interfaces.json'))[team]
                    expected_subs = {'/team_' + team + '/perception', '/referee/mission',
                                     '/referee/event_text', '/referee/team_registry'}
                    expected_pubs = {'/team_' + team + '/action', '/team_' + team + '/registration'}
                    subs, pubs = set(interface['subscriptions']), set(interface['publishers'])
                    if (not expected_subs <= subs or subs - expected_subs - {'/parameter_events'}
                            or not expected_pubs <= pubs or pubs - expected_pubs - {'/parameter_events', '/rosout'}):
                        raise ValueError(f'Unexpected controller interface: {directory}')
            config = read_json(path / 'judge' / row['session_id'] / 'config.json')
            cases[key] = dict(path=str(path), run=run, row=row, config=config,
                              car_decisions=sum(c['decisions'] for c in controllers.values()),
                              receipt_tick_mismatches=receipt_mismatches,
                              receipt_action_mismatches=receipt_action_mismatches,
                              missing_receipts=missing_receipts)
    return cases


def validate_profile_pair(old, new):
    x, y = old['row'], new['row']
    if (x['seat'] != y['seat'] or x['arrangement'] != y['arrangement']
            or old['run']['lineup_schedule'] != new['run']['lineup_schedule']):
        raise ValueError('Unequal profile schedule')
    for team in 'ABCD':
        if team == x['seat']:
            continue
        a, b = x['controllers'][team], y['controllers'][team]
        if any(a.get(k) != b.get(k) for k in ['profile', 'binary_sha256', 'planner_params']):
            raise ValueError('Unequal heterogeneous opponent: ' + team)
    if old['run'].get('evaluator_sha256') != new['run'].get('evaluator_sha256') or old['run'].get('evaluator_modules') != new['run'].get('evaluator_modules'):
        raise ValueError('Profile cohorts must use the same frozen evaluator')


def compare(controls, candidates):
    a, b = load_batches(controls), load_batches(candidates)
    if not a or a.keys() != b.keys():
        raise ValueError('Cohorts must have the same nonempty scenario/seat/repetition set')
    pairs = []
    for key in sorted(a):
        old, new = a[key], b[key]
        for field in ['binary_sha256', 'judge_core_source_sha256', 'damage_cap', 'tick_ms',
                      'max_ticks', 'no_events', 'ai', 'opponents', 'candidate_count', 'opponent_params']:
            if field == 'binary_sha256' and old['run'].get('profiles'):
                continue
            if old['run'].get(field) != new['run'].get(field):
                raise ValueError(f'Unequal {field}: {key}')
        if bool(old['run'].get('profiles')) != bool(new['run'].get('profiles')):
            raise ValueError('Cannot mix legacy and profile cohorts')
        if old['run'].get('profiles'):
            validate_profile_pair(old, new)
        if old['config'] != new['config']:
            raise ValueError(f'Unequal deployed configuration: {key}')
        x, y = old['row'], new['row']
        if x.get('candidate_teams') != y.get('candidate_teams'):
            raise ValueError(f'Unequal candidate assignment: {key}')
        pairs.append(dict(case=list(key), score=[x['own']['score'], y['own']['score']],
            resources=[x['own']['resource_value'], y['own']['resource_value']],
            missions=[x['own']['mission_completed'], y['own']['mission_completed']],
            damage=[x['own']['effective_damage'], y['own']['effective_damage']],
            rank=[x['rank'], y['rank']], first=[x['strict_first'], y['strict_first']]))
    return dict(note='Practice scores only. One game with four cars is one game; do not pool private observations.',
        pairs=len(pairs), controlled_car_decisions=[sum(c['car_decisions'] for c in side.values()) for side in [a, b]],
        receipt_tick_mismatches=[sum(c['receipt_tick_mismatches'] for c in side.values()) for side in [a, b]],
        receipt_action_mismatches=[sum(c['receipt_action_mismatches'] for c in side.values()) for side in [a, b]],
        missing_receipts=[sum(c['missing_receipts'] for c in side.values()) for side in [a, b]],
        strict_first=[sum(p['first'][i] for p in pairs) for i in [0, 1]],
        mean_score=[statistics.mean(p['score'][i] for p in pairs) for i in [0, 1]],
        mean_resource=[statistics.mean(p['resources'][i] for p in pairs) for i in [0, 1]],
        mean_rank=[statistics.mean(p['rank'][i] for p in pairs) for i in [0, 1]],
        score_improvements=sum(p['score'][1] > p['score'][0] for p in pairs),
        score_regressions=sum(p['score'][1] < p['score'][0] for p in pairs),
        rank_regressions=sum(p['rank'][1] > p['rank'][0] for p in pairs),
        changed=[p for p in pairs if any(p[k][0] != p[k][1] for k in ['score', 'resources', 'missions', 'damage', 'rank'])])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', nargs='+', type=Path, required=True)
    parser.add_argument('--candidate', nargs='+', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(compare(args.control, args.candidate), ensure_ascii=False, indent=2))
