"""Development-only paired stage-one suite. Fixtures never enter contestant inputs."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import threading
import time


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace',type=Path,required=True)
    parser.add_argument('--judge-root',type=Path,required=True)
    parser.add_argument('--dataset',type=Path,required=True)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--label',required=True)
    parser.add_argument('--groups',nargs='+')
    parser.add_argument('--candidate-param',action='append',default=[])
    parser.add_argument('--workers',type=int,default=4,choices=range(1,5))
    args=parser.parse_args()
    if args.candidate_param not in [[],['stage1_joint_coverage:=true'],['stage1_joint_coverage:=true','stage1_subgoal_risk:=true']]:parser.error('Unsupported stage-one candidate flags')
    cases=json.loads(args.dataset.read_text())
    if args.groups:cases=[c for c in cases if c['group'] in args.groups]
    if not cases or len({c['case_id'] for c in cases})!=len(cases):raise ValueError('Missing or duplicate cases')
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    for c in cases:
        if digest(Path(c['config_file']))!=c['config_sha256']:raise ValueError('Fixture changed')
    output=args.workspace/'suites'/(args.label+'-'+str(time.time_ns()));output.mkdir(parents=True)
    binary=output/'contestant';shutil.copy2(args.binary,binary)
    driver=output/'stage1_eval.py';shutil.copy2(Path(__file__).with_name('stage1_eval.py'),driver)
    shutil.copy2(__file__,output/'stage1_suite.py')
    manifest=dict(label=args.label,binary_sha256=digest(binary),evaluator_sha256=digest(driver),
        cases=cases,candidate_params=args.candidate_param,expected_games=2*len(cases),workers=args.workers,status='running')
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))
    jobs=[(c,arm) for c in cases for arm in ['control','candidate']]
    results=[];errors=[];lock=threading.Lock();stop=threading.Event()
    def worker(index):
        for case,arm in jobs[index::args.workers]:
            if stop.is_set():return
            label=args.label+'-'+case['case_id']+'-'+arm
            command=[sys.executable,str(driver),'--workspace',str(args.workspace),'--judge-root',str(args.judge_root),
                '--binary',str(binary),'--config-file',case['config_file'],'--seed',str(case['seed']),
                '--label',label,'--param','stage1_resource_route:='+str(arm=='candidate').lower(),
                '--domain',str(84+2*index),'--port',str(8484+2*index)]
            if arm=='candidate':
                for param in args.candidate_param:command+=['--param',param]
            stdout=output/(case['case_id']+'-'+arm+'.stdout')
            try:
                with stdout.open('w') as stream:
                    process=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
                    try:code=process.wait(timeout=100)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid,signal.SIGINT)
                        try:process.wait(timeout=20)
                        except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
                        raise TimeoutError('Stage-one run exceeded 100 seconds')
                if code:raise RuntimeError('Driver failed; inspect '+str(stdout))
                lines=stdout.read_text().splitlines();summary=json.loads(lines[-1])
                row=dict(case_id=case['case_id'],group=case['group'],seed=case['seed'],arm=arm,
                         output=summary['output'],summary=summary)
                with lock:
                    results.append(row);(output/'results.json').write_text(json.dumps(results,indent=2))
                    print(json.dumps(dict(case=case['case_id'],arm=arm,score=summary['own']['score'],done=len(results),total=len(jobs))),flush=True)
            except BaseException as error:
                stop.set()
                with lock:errors.append(dict(case_id=case['case_id'],arm=arm,error=str(error)))
                return
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures=[pool.submit(worker,i) for i in range(args.workers)]
        for future in futures:future.result()
    manifest.update(status='failed' if errors or len(results)!=len(jobs) else 'complete',completed_games=len(results),errors=errors)
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))
    print(json.dumps(dict(output=str(output),status=manifest['status'],games=len(results))),flush=True)
    if manifest['status']!='complete':raise SystemExit(1)


if __name__=='__main__':main()
