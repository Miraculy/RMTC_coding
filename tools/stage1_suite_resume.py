"""Resume only missing cases after a verified pre-start infrastructure failure."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time


def read(path):return json.loads(path.read_text())
def write(path,data):path.write_text(json.dumps(data,indent=2))
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite',type=Path,required=True)
    parser.add_argument('--judge-root',type=Path,required=True)
    args=parser.parse_args();suite=args.suite;workspace=suite.parent.parent
    manifest=read(suite/'manifest.json');results=read(suite/'results.json')
    if manifest['status']!='failed':raise ValueError('Only an explicitly failed suite can be resumed')
    for filename,key in [('contestant','binary_sha256'),('stage1_eval.py','evaluator_sha256')]:
        if digest(suite/filename)!=manifest[key]:raise ValueError('Frozen test component changed')
    cases={c['case_id']:c for c in manifest['cases']}
    for case in cases.values():
        if digest(Path(case['config_file']))!=case['config_sha256']:raise ValueError('Fixture changed')
    failures=[]
    for error in manifest['errors']:
        case=cases[error['case_id']];arm=error['arm']
        attempts=list((workspace/'test-results').glob(f"{manifest['label']}-{case['case_id']}-{arm}*-{case['seed']}-*"))
        invalid=[p for p in attempts if not (p/'summary.json').exists()]
        if not invalid:raise ValueError('No retained failed attempt')
        for path in invalid:
            records=[json.loads(l) for p in (path/'own').glob('*.jsonl') for l in p.read_text().splitlines()]
            if records:raise ValueError('A started game cannot be silently replaced')
            failures.append(dict(case_id=case['case_id'],arm=arm,output=str(path),record_count=0,
                reason='Interface discovery validation stopped the paused judge before resume; no car decision or score result exists.'))
    if not failures:raise ValueError('No auditable pre-start failure')
    complete={(r['case_id'],r['arm']) for r in results}
    jobs=[(c,a) for c in cases.values() for a in ['control','candidate'] if (c['case_id'],a) not in complete]
    epoch=len(manifest.get('failure_history',[]))+1
    write(suite/f'manifest.failed-{epoch}.json',manifest)
    manifest.setdefault('failure_history',[]).append(dict(epoch=epoch,errors=manifest['errors'],failed_attempts=failures,
        retained_games=len(results),missing_games=len(jobs),resumer_sha256=digest(Path(__file__))))
    manifest.update(status='running',errors=[]);write(suite/'manifest.json',manifest)
    (suite/f'stage1_suite_resume-{epoch}.py').write_bytes(Path(__file__).read_bytes())
    lock=threading.Lock();stop=threading.Event();errors=[];workers=manifest['workers']
    def worker(index):
        for case,arm in jobs[index::workers]:
            if stop.is_set():return
            stdout=suite/f"{case['case_id']}-{arm}.resume-{epoch}.stdout"
            command=[sys.executable,str(suite/'stage1_eval.py'),'--workspace',str(workspace),'--judge-root',str(args.judge_root),
                '--binary',str(suite/'contestant'),'--config-file',case['config_file'],'--seed',str(case['seed']),
                '--label',manifest['label']+'-'+case['case_id']+'-'+arm+'-resume'+str(epoch),
                '--param','stage1_resource_route:='+str(arm=='candidate').lower(),'--domain',str(84+2*index),'--port',str(8484+2*index)]
            if arm=='candidate':
                for param in manifest.get('candidate_params',[]):command+=['--param',param]
            try:
                with stdout.open('w') as stream:
                    process=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
                    try:code=process.wait(timeout=100)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid,signal.SIGINT)
                        try:process.wait(timeout=20)
                        except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
                        raise TimeoutError('Resumed driver exceeded 100 seconds')
                if code:raise RuntimeError('Driver failed; inspect '+str(stdout))
                summary=json.loads(stdout.read_text().splitlines()[-1])
                row=dict(case_id=case['case_id'],group=case['group'],seed=case['seed'],arm=arm,
                    output=summary['output'],summary=summary,resume_epoch=epoch)
                with lock:
                    results.append(row);write(suite/'results.json',results)
                    print(json.dumps(dict(case=case['case_id'],arm=arm,done=len(results),total=manifest['expected_games'])),flush=True)
            except BaseException as error:
                stop.set()
                with lock:errors.append(dict(case_id=case['case_id'],arm=arm,error=str(error)))
                return
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures=[pool.submit(worker,i) for i in range(workers)]
        for future in futures:future.result()
    manifest.update(status='failed' if errors or len(results)!=manifest['expected_games'] else 'complete',completed_games=len(results),errors=errors)
    write(suite/'manifest.json',manifest);print(json.dumps(dict(status=manifest['status'],games=len(results))),flush=True)
    if manifest['status']!='complete':raise SystemExit(1)


if __name__=='__main__':main()
