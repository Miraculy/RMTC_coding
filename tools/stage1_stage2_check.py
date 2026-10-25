"""Compare complete stage-two games using each car's own logs and final board."""
import argparse
import hashlib
import json
from pathlib import Path


def read(path):
    return json.loads(path.read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize(value):
    if isinstance(value, dict):
        return {k: normalize(v) for k, v in value.items()
                if k not in {'session_id', 'wall_ms', 'latency_ms', 'stage1_route_evaluation'}}
    if isinstance(value, list):
        return [normalize(v) for v in value]
    return value


def compare(control, candidate):
    roots = [control, candidate]
    runs = [read(root / 'run.json') for root in roots]
    summaries = [read(root / 'summary.json') for root in roots]
    for root, run in zip(roots, runs):
        if digest(root / 'contestant') != run['binary_sha256'] or digest(root / 'strategy_eval.py') != run['evaluator_sha256']:
            raise ValueError('Frozen executable/evaluator changed')
        for name, expected in run['evaluator_modules'].items():
            if digest(root / name) != expected:
                raise ValueError('Frozen evaluator dependency changed')
        if run['max_ticks'] != 500 or run['tick_ms'] != 100 or run['ai'] != 'off' or run['opponents'] != 'self':
            raise ValueError('Wrong regression configuration')
    for key in ['judge_manifest_sha256', 'judge_core_source_sha256', 'max_ticks', 'tick_ms', 'no_events', 'variants', 'seats', 'repetitions']:
        if runs[0][key] != runs[1][key]:
            raise ValueError('Inconsistent paired configuration: ' + key)
    if runs[0]['planner_params'] or runs[1]['planner_params'] not in [['stage1_resource_route:=true'],['stage1_resource_route:=true','stage1_joint_coverage:=true']]:
        raise ValueError('Wrong stage-one flag ablation')
    cases = [{row['variant']: row for row in summary} for summary in summaries]
    if set(cases[0]) != set(runs[0]['variants'].split(',')) or set(cases[0]) != set(cases[1]):
        raise ValueError('Missing planned maps')
    sessions = set()
    decisions = interfaces = receipts = 0
    rows = []
    max_latency = max_p95 = 0
    for variant in sorted(cases[0]):
        paired_frames = []
        for side, root in enumerate(roots):
            result = cases[side][variant]
            if len(result['controllers']) != 4:
                raise ValueError('Missing independent car')
            label = variant + '-A'
            graph = read(root / (label + '-interfaces.json'))
            state = read(root / (label + '-state.json'))
            by_team = {}
            for team, controller in result['controllers'].items():
                expected_subs = {'/team_' + team + '/perception', '/referee/mission', '/referee/event_text', '/referee/team_registry'}
                expected_pubs = {'/team_' + team + '/action', '/team_' + team + '/registration'}
                subs, pubs = set(graph[team]['subscriptions']), set(graph[team]['publishers'])
                if not expected_subs <= subs or subs - expected_subs - {'/parameter_events'}:
                    raise ValueError('Foreign or missing subscription')
                if not expected_pubs <= pubs or pubs - expected_pubs - {'/parameter_events', '/rosout'}:
                    raise ValueError('Unexpected publisher')
                interfaces += 1
                records = [json.loads(line) for path in (root / controller['log_dir']).glob('*.jsonl') for line in path.read_text().splitlines()]
                frames = [r for r in records if r.get('kind') == 'decision']
                if [r['tick'] for r in frames] != list(range(500)):
                    raise ValueError('Incomplete control')
                session = frames[0]['session_id']
                if any(f['session_id'] != session for f in frames):
                    raise ValueError('Mixed sessions in one car')
                if (session, team) in sessions:
                    raise ValueError('Duplicate car session')
                sessions.add((session, team))
                if any(r.get('kind') in {'perception_rejected', 'ai'} for r in records) or controller['own']['invalid_actions']:
                    raise ValueError('Rejected/illegal input or unexpected model call')
                for previous, current in zip(frames, frames[1:]):
                    receipt = current.get('last_action') or {}
                    if receipt.get('tick') != previous['tick'] or receipt.get('action') != previous['action']['action']:
                        raise ValueError('Receipt mismatch')
                receipt = state['checks'][team]['last_action']
                if receipt['tick'] != 499 or receipt['action'] != frames[-1]['action']['action']:
                    raise ValueError('Final Tick499 receipt mismatch')
                if next(t for t in state['board']['teams'] if t['team'] == team) != controller['own']:
                    raise ValueError('Final board mismatch')
                max_p95 = max(max_p95, controller['p95_ms'])
                max_latency = max(max_latency, controller['max_ms'])
                decisions += len(frames)
                receipts += len(frames)
                by_team[team] = frames
            paired_frames.append(by_team)
        if cases[0][variant]['teams'] != cases[1][variant]['teams']:
            raise ValueError('Gameplay board differs')
        for team in 'ABCD':
            for a, b in zip(paired_frames[0][team], paired_frames[1][team]):
                if normalize(a) != normalize(b):
                    raise ValueError(f'Gameplay differs at {variant}/{team}/Tick{a["tick"]}')
        rows.append(dict(variant=variant, gameplay_decision_pairs=2000, board=cases[1][variant]['teams']))
    return dict(control=str(control), candidate=str(candidate), gameplay_equal=True, maps=len(rows),
        decisions=decisions, gameplay_decision_pairs=decisions // 2, interfaces=interfaces, receipt_checks=receipts,
        invalid_actions=0, rejected_frames=0, missing_ticks=0, receipt_errors=0, max_p95_ms=max_p95, max_ms=max_latency,
        excluded_fields=['session_id', 'wall_ms', 'latency_ms', 'stage1_route_evaluation'],
        actual_parameter_dump_available=False, flag_evidence='Frozen launch arguments, declared node parameter, and stage-two guard unit test; no runtime parameter dump was captured by this legacy driver.',
        cases=rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('control', type=Path)
    parser.add_argument('candidate', type=Path)
    args = parser.parse_args()
    print(json.dumps(compare(args.control, args.candidate), ensure_ascii=False, indent=2))
