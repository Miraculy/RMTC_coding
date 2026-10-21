"""Development-only judge orchestration; never imported by the C++ contestant."""
import argparse, json, os, pathlib, signal, subprocess, time, urllib.request
parser=argparse.ArgumentParser()
parser.add_argument("--standard",action="store_true")
parser.add_argument("--stage2-only",action="store_true")
args=parser.parse_args()
ROOT=pathlib.Path('/root/ros2_ws')
OUT=ROOT/'test-results'; OUT.mkdir(exist_ok=True)
BASE='http://127.0.0.1:8081'
ENV=dict(os.environ, ROS_DOMAIN_ID='40',RMTC_INTERNAL_DOMAIN_ID='41',ROS_LOCALHOST_ONLY='0',RMW_IMPLEMENTATION='rmw_fastrtps_cpp',RMTC_TEAM_TOKEN='practice-A')
children=[]; streams=[]
def launch(args,name):
    f=(OUT/(name+'.log')).open('w');streams.append(f)
    p=subprocess.Popen(args,env=ENV,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);children.append(p);return p
def api(path,data=None):
    req=urllib.request.Request(BASE+path,data=None if data is None else json.dumps(data).encode(),headers={'Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=20) as r:return json.load(r)
def wait_ready():
    end=time.monotonic()+20
    while time.monotonic()<end:
        try:
            s=api('/api/state')
            if s.get('status',{}).get('ready'):return s
        except Exception:pass
        time.sleep(.2)
    raise RuntimeError('judge not ready')
def finish(timeout):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        s=api('/api/state')
        if s.get('fault'):raise RuntimeError(s['fault'])
        if s['status'].get('finished'):return s
        time.sleep(.5)
    raise RuntimeError('match timed out')
def stop(p):
    if p.poll() is None:
        os.killpg(p.pid,signal.SIGINT)
        try:p.wait(timeout=15)
        except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
def contestant(stage,label,max_ticks=None,ai=True):
    return launch([str(ROOT/'install/rmtc_bot/lib/rmtc_bot/contestant'),'--ros-args','-p',f'stage:={stage}','-p',f'max_ticks:={max_ticks or (100 if stage==1 else 500)}','-p','ai_gateway:=http://127.0.0.1:8081/v1/chat/completions','-p',f'ai_enabled:={str(ai).lower()}','-p',f'log_dir:={OUT/label}'],label)
def summarize(s,label):
    (OUT/(label+'.json')).write_text(json.dumps(s,ensure_ascii=False,indent=2))
    rows=[]
    for f in (OUT/label).glob('*.jsonl'):
        for line in f.read_text().splitlines():
            try:rows.append(json.loads(line))
            except ValueError:pass
    session=s.get('session_id',s.get('board',{}).get('session_id'))
    rows=[r for r in rows if r.get('session_id')==session]
    decisions=[r for r in rows if r['kind']=='decision']
    lats=sorted(r['latency_ms'] for r in decisions)
    out={'label':label,'tick':s['status'].get('tick'),'teams':s['board'].get('teams'),
         'decisions':len(decisions),'max_latency_ms':max(lats,default=0),'p95_latency_ms':lats[int(len(lats)*.95)] if lats else 0,
         'rejected_frames':sum(r['kind']=='perception_rejected' for r in rows),
         'action_types':{kind:sum(r['action']['action']==kind for r in decisions) for kind in ['move','attack','defend','wait']},
         'ai_http_successes':sum(r['kind']=='ai' and r['http_success'] for r in rows)}
    print(json.dumps(out,ensure_ascii=False),flush=True);return out
results=[]
try:
    judge=launch(['/usr/bin/python3','/root/rmtc_practice/practice.py','--stage','1','--port','8081','--data-dir',str(OUT/'judge')],'judge')
    wait_ready()
    if not args.stage2_only:
        bot=contestant(1,'stage1',ai=False);time.sleep(1.5);api('/api/resume',{});s=finish(25);results.append(summarize(s,'stage1'));stop(bot)
        assert next(t for t in s['board']['teams'] if t['team']=='A')['invalid_actions']==0
        assert next(t for t in s['board']['teams'] if t['team']=='A')['resource_value']>=120
    api('/api/start',{'stage':2,'tick_ms':500 if args.standard else 50,'max_ticks':500,'bots':['B','C','D']})
    label='stage2_standard' if args.standard else 'stage2_fast'
    wait_ready();bot=contestant(2,label);time.sleep(1.5);api('/api/resume',{});s=finish(300 if args.standard else 45);results.append(summarize(s,label));stop(bot)
    assert next(t for t in s['board']['teams'] if t['team']=='A')['invalid_actions']==0
    (OUT/'integration-summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2))
finally:
    for p in reversed(children):stop(p)
    for f in streams:f.close()
