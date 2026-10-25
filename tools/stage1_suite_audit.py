"""Read-only validation of complete stage-one paired suites and legal own logs."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import statistics


def read(path):return json.loads(path.read_text())
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def quantile(values,fraction):
    values=sorted(values);index=(len(values)-1)*fraction;lo=int(index)
    return values[lo]+(values[min(lo+1,len(values)-1)]-values[lo])*(index-lo)


def audit(suite):
    manifest=read(suite/'manifest.json');results=read(suite/'results.json')
    if manifest['status']!='complete' or len(results)!=manifest['expected_games']:raise ValueError('Incomplete planned suite')
    if digest(suite/'contestant')!=manifest['binary_sha256'] or digest(suite/'stage1_eval.py')!=manifest['evaluator_sha256']:
        raise ValueError('Suite executable/evaluator changed')
    cases={c['case_id']:c for c in manifest['cases']}
    if {(r['case_id'],r['arm']) for r in results}!={(c,a) for c in cases for a in ['control','candidate']}:
        raise ValueError('Wrong paired case set')
    prestart_failures=[]
    for history in manifest.get('failure_history',[]):
        snapshot=suite/('stage1_suite_resume-'+str(history['epoch'])+'.py')
        if digest(snapshot)!=history['resumer_sha256']:raise ValueError('Resume procedure changed')
        for attempt in history['failed_attempts']:
            failed=Path(attempt['output'])
            records=[line for p in (failed/'own').glob('*.jsonl') for line in p.read_text().splitlines()]
            if records or (failed/'summary.json').exists() or attempt['record_count']!=0:
                raise ValueError('A started match was substituted')
            prestart_failures.append(attempt)
    sessions=set();judge_versions=set();checked=[];solver_incomplete=0;verified_promises=0
    for result in results:
        path=Path(result['output']);run=read(path/'run.json');summary=read(path/'summary.json');case=cases[result['case_id']]
        if digest(Path(case['config_file']))!=case['config_sha256']:raise ValueError('Preregistered fixture changed')
        if run.get('stage')!=1 or run.get('tick_ms')!=100 or run.get('max_ticks')!=100 or run.get('ai') is not False:
            raise ValueError('Wrong competition timing, stage, or model mode')
        judge_versions.add((run['judge_manifest_sha256'],run['judge_core_source_sha256']))
        config=read(path/'config.json')
        if config!=read(Path(case['config_file'])):raise ValueError('Deployed fixture differs')
        if digest(path/'config.json')!=run['config_sha256'] or digest(path/'contestant')!=manifest['binary_sha256']:
            raise ValueError('Frozen fixture or binary differs')
        if digest(path/'stage1_eval.py')!=manifest['evaluator_sha256']:raise ValueError('Evaluator snapshot differs')
        parameters=read(path/'parameters.json')
        if parameters.get('team')!='A' or parameters.get('stage')!=1 or parameters.get('stage1_resource_route') is not (result['arm']=='candidate'):
            raise ValueError('Actual parameters differ')
        for param in manifest.get('candidate_params',[]):
            key,value=param.split(':=',1);expected=(result['arm']=='candidate' and value=='true')
            if parameters.get(key) is not expected:raise ValueError('Additional candidate parameter differs')
        interface=read(path/'interfaces.json');subs=set(interface['subscriptions']);pubs=set(interface['publishers'])
        expected={'/team_A/perception','/referee/mission','/referee/event_text','/referee/team_registry'}
        if not expected<=subs or subs-expected-{'/parameter_events'}:raise ValueError('Illegal subscription')
        expected={'/team_A/action','/team_A/registration'}
        if not expected<=pubs or pubs-expected-{'/parameter_events','/rosout'}:raise ValueError('Illegal publisher')
        records=[json.loads(line) for p in (path/'own').glob('*.jsonl') for line in p.read_text().splitlines()]
        frames=[r for r in records if r.get('kind')=='decision']
        if [r['tick'] for r in frames]!=list(range(100)):raise ValueError('Incomplete tick coverage')
        if len({r['session_id'] for r in frames})!=1 or frames[0]['session_id'] in sessions:raise ValueError('Duplicate or mixed session')
        sessions.add(frames[0]['session_id'])
        if summary['own']['invalid_actions'] or any(r.get('kind')=='perception_rejected' for r in records):raise ValueError('Illegal/rejected frame')
        for f in frames[1:]:
            receipt=f.get('last_action')
            if not receipt or receipt.get('tick')!=f['tick']-1 or receipt.get('action')!=frames[f['tick']-1]['action']['action']:
                raise ValueError('Missing or mismatched receipt')
        if any(r.get('kind')=='ai' for r in records):raise ValueError('Unexpected model use')
        state=read(path/'state.json')
        final_receipt=state.get('checks',{}).get('A',{}).get('last_action')
        if not final_receipt or final_receipt.get('tick')!=99 or final_receipt.get('action')!=frames[-1]['action']['action']:
            raise ValueError('Final action99 receipt mismatch')
        board=next(t for t in state['board']['teams'] if t['team']=='A')
        if board!=summary['own'] or abs(board['score']-.2*board['resource_value'])>1e-8:raise ValueError('Wrong final score')
        # Only the certified candidate labels these as executable guarantees.
        # Optimistic experimental variants do not receive this interpretation.
        for frame in frames:
            route=frame.get('stage1_route_evaluation') or {}
            if route.get('distance_model')!='observed_free_route_with_optimistic_value_bound' or not route.get('route'):continue
            if route['planned_steps']>100-frame['tick']:raise ValueError('Certified route exceeds remaining budget')
            if frame['resource_value']+route['planned_value']>board['resource_value']+1e-8:
                raise ValueError('Certified route value was not retained through the finish')
            verified_promises+=1
        # Own confirmed collection positions and delta, not full-map guesses.
        high=normal=0
        for previous,current in zip(frames,frames[1:]):
            if current['resource_value']<=previous['resource_value']:continue
            value=current['resource_value']-previous['resource_value']
            if value>=20:high+=1
            else:normal+=1
        final=next((r for r in reversed(records) if r.get('kind')=='final'),None)
        if final and final['resource_value']>frames[-1]['resource_value']:
            if final['resource_value']-frames[-1]['resource_value']>=20:high+=1
            else:normal+=1
        solver_incomplete+=sum(bool(r.get('stage1_route_evaluation')) and not r['stage1_route_evaluation'].get('solver_complete',True) for r in frames)
        checked.append(dict(case_id=result['case_id'],group=result['group'],seed=result['seed'],arm=result['arm'],
            score=board['score'],resource=board['resource_value'],high=high,normal=normal,
            p95_ms=summary['p95_ms'],max_ms=summary['max_ms'],resource_by_tick=summary['resource_by_tick']))
    if len(judge_versions)!=1:raise ValueError('Mixed judge versions')
    groups={}
    for group in sorted({r['group'] for r in checked}):
        sides={a:[r for r in checked if r['group']==group and r['arm']==a] for a in ['control','candidate']}
        aggregate={}
        for arm,rows in sides.items():
            values=[r['score'] for r in rows]
            aggregate[arm]=dict(games=len(rows),mean=statistics.mean(values),median=statistics.median(values),
                p10=quantile(values,.1),minimum=min(values),full=sum(v==40 for v in values),
                high=sum(r['high'] for r in rows),normal=sum(r['normal'] for r in rows))
        base={r['case_id']:r for r in sides['control']};new={r['case_id']:r for r in sides['candidate']}
        gains=[new[k]['score']-base[k]['score'] for k in base]
        aggregate['paired']=dict(improved=sum(v>0 for v in gains),regressed=sum(v<0 for v in gains),worst_change=min(gains))
        aggregate['stability_passed']=(aggregate['candidate']['p10']>=aggregate['control']['p10'] and aggregate['candidate']['minimum']>=aggregate['control']['minimum'] and min(gains)>=-2)
        aggregate['mean_passed']=aggregate['candidate']['mean']>=aggregate['control']['mean']+(2 if group=='known25' else 0) and (group=='known25' or aggregate['candidate']['mean']>aggregate['control']['mean'])
        groups[group]=aggregate
    gates=dict(stability=all(g['stability_passed'] for g in groups.values()),
        mean=all(g['mean_passed'] for g in groups.values()),
        more_full_known=('known25' in groups and groups['known25']['candidate']['full']>groups['known25']['control']['full']),
        callback_budget=max(r['p95_ms'] for r in checked)<=25 and max(r['max_ms'] for r in checked)<=50)
    return dict(stage1_gates=gates,stage1_gates_passed=all(gates.values()),suite=str(suite),games=len(results),decisions=100*len(results),interfaces=len(results),
        parameter_audits=len(results),prestart_failed_attempts=prestart_failures,resumed_games=sum('resume_epoch' in r for r in results),invalid_actions=0,rejected_frames=0,receipt_errors=0,
        max_p95_ms=max(r['p95_ms'] for r in checked),max_ms=max(r['max_ms'] for r in checked),
        solver_incomplete_frames=solver_incomplete,verified_route_promises=verified_promises,groups=groups,cases=checked,
        note='Each map is a paired game, not an independent sample per tick. Oracle data are excluded from online inputs.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('suite',type=Path);args=parser.parse_args()
    print(json.dumps(audit(args.suite),ensure_ascii=False,indent=2))
