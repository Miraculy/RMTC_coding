"""Summarize development simulations; never a final promotion audit."""
import argparse
import json
import hashlib
from pathlib import Path
import statistics


def quantile(values, p):
    values=sorted(values);i=(len(values)-1)*p;lo=int(i)
    return values[lo]+(values[min(lo+1,len(values)-1)]-values[lo])*(i-lo)


def summarize(workspace, result):
    cases=json.loads((workspace/'dev-input.json').read_text())
    baseline=json.loads((workspace/'sim-control-development.json').read_text())
    candidate=json.loads(result.read_text())
    if len(cases)!=len(baseline) or len(cases)!=len(candidate):raise ValueError('Incomplete fixture set')
    for i,case in enumerate(cases):
        if any(rows[i].get('seed')!=case['seed'] for rows in [baseline,candidate]):raise ValueError('Fixture order or seed differs')
        for rows in [baseline,candidate]:
            for key in ['cohort','group']:
                if key in rows[i] and rows[i][key]!=case[key]:raise ValueError('Fixture cohort/group differs')
    groups={};regressions=[]
    for cohort,group in sorted({(c['cohort'],c['group']) for c in cases}):
        ids=[i for i,c in enumerate(cases) if (c['cohort'],c['group'])==(cohort,group)]
        sides={}
        for label,rows in [('control',baseline),('candidate',candidate)]:
            scores=[rows[i]['score'] for i in ids]
            sides[label]=dict(mean=statistics.mean(scores),p10=quantile(scores,.1),minimum=min(scores),full=scores.count(40))
        delta=[candidate[i]['score']-baseline[i]['score'] for i in ids]
        sides.update(games=len(ids),improved=sum(v>0 for v in delta),regressed=sum(v<0 for v in delta),worst=min(delta))
        sides['stability_passed']=sides['candidate']['p10']>=sides['control']['p10'] and sides['candidate']['minimum']>=sides['control']['minimum'] and min(delta)>=-2
        groups[cohort+'/'+group]=sides
    for i,c in enumerate(cases):
        change=candidate[i]['score']-baseline[i]['score']
        if change<0:regressions.append(dict(cohort=c['cohort'],group=c['group'],seed=c['seed'],control=baseline[i]['score'],candidate=candidate[i]['score'],change=change))
    hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [workspace/'dev-input.json',workspace/'sim-control-development.json',result]}
    return dict(result=str(result),input_hashes=hashes,games=len(cases),groups=groups,regressions=sorted(regressions,key=lambda r:r['change']),
        invalid_actions=sum(r['invalid_actions'] for r in candidate),maximum_ms=max(r['max_ms'] for r in candidate),
        note='Development own-view simulations only; no assertion of a real-referee score or independent validation.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('workspace',type=Path);parser.add_argument('result',type=Path);args=parser.parse_args()
    print(json.dumps(summarize(args.workspace,args.result),ensure_ascii=False,indent=2))
