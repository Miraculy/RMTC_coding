"""Development-only stage-one random-map check; no fixture data enters a car."""
import argparse
import collections
import hashlib
import json
import math
import os
from pathlib import Path
import random
import shutil
import signal
import subprocess
import time
import tempfile
import urllib.request


def generate(template, seed, obstacle_ratio=0.1):
    config = json.loads(json.dumps(template))
    rng = random.Random(seed)
    width, height = config['map']['width'], config['map']['height']
    spawn = (rng.randrange(1, width-1), rng.randrange(1, height-1))
    config['robots'] = [dict(team='A', spawn=list(spawn))]
    cells = [(x,y) for x in range(width) for y in range(height) if (x,y) != spawn]
    rng.shuffle(cells)
    blocked = set()
    for cell in cells:
        if len(blocked) >= math.ceil(width*height*obstacle_ratio-1e-9):
            break
        candidate = blocked | {cell}
        seen, stack = {spawn}, [spawn]
        while stack:
            x,y = stack.pop()
            for dx,dy in [(0,-1),(1,0),(0,1),(-1,0)]:
                q = x+dx,y+dy
                if 0<=q[0]<width and 0<=q[1]<height and q not in candidate and q not in seen:
                    seen.add(q);stack.append(q)
        if len(seen) == width*height-len(candidate):
            blocked = candidate
    if len(blocked) != math.ceil(width*height*obstacle_ratio-1e-9):
        raise ValueError('Could not generate the requested connected obstacle density')
    config['map']['obstacles'] = [list(p) for p in sorted(blocked)]
    positions = rng.sample([p for p in cells if p not in blocked],len(config['map']['resources']))
    for resource,pos in zip(config['map']['resources'],positions):
        resource['position'] = list(pos)
    return config


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--judge-root',type=Path,required=True)
    parser.add_argument('--seed',type=int,default=20261024)
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--config-file',type=Path)
    parser.add_argument('--width',type=int)
    parser.add_argument('--height',type=int)
    parser.add_argument('--obstacle-ratio',type=float,default=.1)
    parser.add_argument('--param',action='append',default=[])
    parser.add_argument('--label',default='stage1-random')
    parser.add_argument('--domain',type=int,default=84)
    parser.add_argument('--port',type=int,default=8484)
    args=parser.parse_args()
    if not .0<=args.obstacle_ratio<=.75:parser.error('Obstacle ratio must be between 0 and .75')
    allowed={'stage1_resource_route','stage1_joint_coverage','stage1_subgoal_risk'}
    if any(p.partition(':=')[0] not in allowed or p.partition(':=')[2] not in ['true','false'] for p in args.param) or len({p.partition(':=')[0] for p in args.param})!=len(args.param):
        parser.error('Only distinct stage-one boolean planner overrides are allowed')
    output=args.workspace/'test-results'/(args.label+'-'+str(args.seed)+'-'+str(time.time_ns()))
    output.mkdir(parents=True)
    source=args.binary or args.workspace/'install/rmtc_bot/lib/rmtc_bot/contestant'
    binary=output/'contestant';shutil.copy2(source,binary)
    shutil.copy2(__file__,output/'stage1_eval.py')
    template=json.loads((args.judge_root/'config/stage1.json').read_text())
    if args.width is not None:template['map']['width']=args.width
    if args.height is not None:template['map']['height']=args.height
    config=json.loads(args.config_file.read_text()) if args.config_file else generate(template,args.seed,args.obstacle_ratio)
    if config['stage']!=1 or [r['team'] for r in config['robots']]!=['A']:
        raise ValueError('Only single-car stage-one fixtures are supported')
    (output/'config.json').write_text(json.dumps(config,ensure_ascii=False,indent=2))
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    (output/'run.json').write_text(json.dumps(dict(stage=1,seed=args.seed,tick_ms=100,max_ticks=100,
        binary_sha256=digest(binary),evaluator_sha256=digest(output/'stage1_eval.py'),
        judge_manifest_sha256=digest(args.judge_root/'manifest.json'),
        judge_core_source_sha256=json.loads((args.judge_root/'manifest.json').read_text())['core_source_sha256'],
        domain=args.domain,port=args.port,ai=False,practice_only=True,planner_params=args.param,
        config_sha256=digest(output/'config.json')),indent=2))
    env=dict(os.environ,ROS_DOMAIN_ID=str(args.domain),RMTC_INTERNAL_DOMAIN_ID=str(args.domain+1),
             ROS_LOCALHOST_ONLY='0',RMW_IMPLEMENTATION='rmw_fastrtps_cpp',RMTC_TEAM_TOKEN='practice-A')
    for key in ['DEEPSEEK_API_KEY','PRACTICE_LLM_KEY']:env.pop(key,None)
    own_log_dir=Path(tempfile.mkdtemp(prefix="rmtc-stage1-own-"))
    own_log_dir.chmod(0o755)
    children=[];streams=[]
    def launch(command,label):
        stream=(output/(label+'.log')).open('w');streams.append(stream)
        child=subprocess.Popen(command,env=env,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        children.append(child);return child
    def stop(child):
        if child.poll() is not None:return
        os.killpg(child.pid,signal.SIGINT)
        try:child.wait(timeout=12)
        except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
    def api(path,payload=None):
        req=urllib.request.Request('http://127.0.0.1:'+str(args.port)+path,
            data=None if payload is None else json.dumps(payload).encode(),headers={'Content-Type':'application/json'})
        with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req,timeout=20) as response:
            return json.load(response)
    def wait(predicate,timeout=30):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if children[0].poll() is not None:raise RuntimeError('Judge exited')
            try:state=api('/api/state')
            except OSError:time.sleep(.2);continue
            if state.get('fault'):raise RuntimeError(state['fault'])
            if predicate(state):return state
            time.sleep(.2)
        raise TimeoutError('Judge state timeout')
    try:
        launch(['/usr/bin/python3',str(args.judge_root/'practice.py'),'--stage','1','--port',str(args.port),
                '--data-dir',str(output/'judge')],'judge')
        wait(lambda s:s.get('status',{}).get('ready'))
        api('/api/start',dict(stage=1,tick_ms=100,max_ticks=100,bots=[],config=config))
        wait(lambda s:s.get('status',{}).get('ready'))
        parameters=[]
        for param in args.param:parameters+=['-p',param]
        bot=launch([str(binary),'--ros-args','-r','__node:=rmtc_stage1_check_A','-p','team:=A',
                    '-p','stage:=1','-p','max_ticks:=100','-p','ai_enabled:=false',
                    '-p','log_dir:='+str(own_log_dir)]+parameters,'contestant')
        time.sleep(1.5)
        if bot.poll() is not None:raise RuntimeError('Contestant exited')
        script="""
import json,time,rclpy
rclpy.init();node=rclpy.create_node('rmtc_stage1_graph_audit')
try:
 expected_subs={'/team_A/perception','/referee/mission','/referee/event_text','/referee/team_registry'}
 expected_pubs={'/team_A/action','/team_A/registration'}
 subs=[];pubs=[];deadline=time.monotonic()+10
 while time.monotonic()<deadline:
  if ('rmtc_stage1_check_A','/') in node.get_node_names_and_namespaces():
   subs=[t for t,_ in node.get_subscriber_names_and_types_by_node('rmtc_stage1_check_A','/')]
   pubs=[t for t,_ in node.get_publisher_names_and_types_by_node('rmtc_stage1_check_A','/')]
   if expected_subs<=set(subs) and expected_pubs<=set(pubs):break
  time.sleep(.1)
 print(json.dumps(dict(subscriptions=subs,publishers=pubs)))
finally:node.destroy_node();rclpy.shutdown()
"""
        interfaces=json.loads(subprocess.check_output(['/usr/bin/python3','-c',script],env=env,text=True,timeout=15))
        (output/'interfaces.json').write_text(json.dumps(interfaces,indent=2))
        subs=set(interfaces['subscriptions']);pubs=set(interfaces['publishers'])
        expected={'/team_A/perception','/referee/mission','/referee/event_text','/referee/team_registry'}
        if not expected<=subs or subs-expected-{'/parameter_events'}:raise ValueError('Illegal subscriptions')
        expected={'/team_A/action','/team_A/registration'}
        if not expected<=pubs or pubs-expected-{'/parameter_events','/rosout'}:raise ValueError('Illegal publishers')
        import yaml
        raw=subprocess.check_output(['ros2','param','dump','/rmtc_stage1_check_A'],env=env,text=True,timeout=15)
        data=yaml.safe_load(raw)
        actual=data.get('/rmtc_stage1_check_A',data.get('rmtc_stage1_check_A',{})).get('ros__parameters',{})
        if actual.get('stage')!=1 or actual.get('team')!='A':raise ValueError('Wrong runtime seat/stage')
        for param in args.param:
            key,value=param.split(':=',1)
            if actual.get(key) is not (value=='true'):raise ValueError('Parameter not applied')
        (output/'parameters.json').write_text(json.dumps(actual,indent=2))
        api('/api/resume',{})
        wait(lambda s:s.get('status',{}).get('finished'))
        time.sleep(.4);state=api('/api/state');stop(bot)
        (output/'state.json').write_text(json.dumps(state,ensure_ascii=False,indent=2))
        records=[json.loads(line) for p in own_log_dir.glob('*.jsonl') for line in p.read_text().splitlines()]
        frames=[r for r in records if r.get('kind')=='decision']
        if [r['tick'] for r in frames]!=list(range(100)):raise ValueError('Incomplete control')
        own=next(t for t in state['board']['teams'] if t['team']=='A')
        receipts=[r for r in frames if r['tick']>0]
        tick_errors=sum(not r.get('last_action') or r['last_action'].get('tick')!=r['tick']-1 for r in receipts)
        action_errors=sum(bool(r.get('last_action')) and r['last_action'].get('action')!=frames[r['tick']-1]['action']['action'] for r in receipts)
        last=state.get('checks',{}).get('A',{}).get('last_action')
        tick_errors+=not last or last.get('tick')!=99
        action_errors+=bool(last) and last.get('action')!=frames[-1]['action']['action']
        if own['invalid_actions'] or tick_errors or action_errors or any(r.get('kind')=='perception_rejected' for r in records):
            raise ValueError('Illegal actions or receipt/control failure')
        if abs(own['score']-.2*own['resource_value'])>1e-8:raise ValueError('Unexpected stage1 score')
        times=sorted(r['latency_ms'] for r in frames)
        summary=dict(output=str(output),seed=args.seed,own=own,width=config['map']['width'],height=config['map']['height'],
            spawn=config['robots'][0]['spawn'],obstacles=len(config['map']['obstacles']),resource_count=len(config['map']['resources']),
            total_resource=sum(r['value'] for r in config['map']['resources']),decisions=len(frames),
            binary_sha256=digest(binary),planner_params=args.param,
            frames_complete=True,receipt_checks=100,receipt_tick_errors=tick_errors,receipt_action_errors=action_errors,
            ai_calls=sum(r.get('kind')=='ai' for r in records),actions=dict(collections.Counter(r['action']['action'] for r in frames)),
            p95_ms=times[int(.95*(len(times)-1))],max_ms=max(times),
            resource_by_tick={str(t):frames[t]['resource_value'] for t in [0,10,20,40,60,80,99]},
            note='One preregistered random map, not a general success rate. Fixture only used by driver; contestant gets own legal perception.')
        (output/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2));print(json.dumps(summary,ensure_ascii=False),flush=True)
    finally:
        for child in reversed(children):stop(child)
        for stream in streams:stream.close()
        shutil.copytree(own_log_dir,output/'own',dirs_exist_ok=True)
        shutil.rmtree(own_log_dir)


if __name__=='__main__':main()
