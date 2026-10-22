"""Opt-in paid DeepSeek test: continuous public ROS frames while real AI runs.

Run from a sourced ROS Humble environment. Key comes from DEEPSEEK_API_KEY or stdin.
No referee/full-map API is used; synthetic public perception is the test input.
"""
import argparse
import json
import os
import pathlib
import signal
import subprocess
import sys
import time
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

parser = argparse.ArgumentParser()
parser.add_argument('--live-deepseek', action='store_true', required=True)
parser.add_argument('--schema-repair-fault', action='store_true', help='Inject one malformed public-event response, then repair with real DeepSeek')
args = parser.parse_args()
key = os.environ.get('DEEPSEEK_API_KEY') or sys.stdin.readline().strip()
if not key:
    raise SystemExit('DEEPSEEK_API_KEY or stdin key is required')
os.environ.update(ROS_DOMAIN_ID='46', ROS_LOCALHOST_ONLY='0', RMW_IMPLEMENTATION='rmw_fastrtps_cpp')
import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
from std_msgs.msg import String

root = pathlib.Path(__file__).resolve().parents[1]
out = root / 'test-results' / f'deepseek-wire-{time.time_ns()}'
out.mkdir(parents=True)
stream = (out / 'process.log').open('w')
env = dict(os.environ, DEEPSEEK_API_KEY=key)
key = None
gateway = 'https://api.deepseek.com/chat/completions'
proxy = None
faults, upstream_calls = 0, 0
cleanup_notice = '世界分裂将在下一 Tick 结束并清理临时十字障碍，原有障碍保留。'
if args.schema_repair_fault:
    import requests
    class FaultProxy(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass  # Never print request headers or response bodies.
        def do_POST(self):
            global faults, upstream_calls
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            public = json.loads(body['messages'][1]['content'])
            if public.get('announcement') == cleanup_notice and not public.get('validation_error') and faults == 0:
                faults += 1
                malformed = {'session_id': public['session_id'], 'events': [
                    {'kind': 'world_split', 'start_tick': 80, 'end_tick': 100, 'points': []}], 'effects': []}
                response = json.dumps({'model': 'fault-fixture', 'choices': [
                    {'finish_reason': 'stop', 'message': {'content': json.dumps(malformed)}}]}).encode()
                status = 200
            else:
                upstream_calls += 1
                try:
                    result = requests.post('https://api.deepseek.com/chat/completions', json=body,
                        headers={'Authorization': self.headers['Authorization']}, timeout=12)
                    response, status = result.content, result.status_code
                except requests.RequestException:
                    response, status = b'{"error":{"code":"upstream_timeout"}}', 504
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(response)))
            self.end_headers()
            self.wfile.write(response)
    proxy = ThreadingHTTPServer(('127.0.0.1', 0), FaultProxy)
    proxy.daemon_threads = True
    proxy_thread = threading.Thread(target=proxy.serve_forever, daemon=True)
    proxy_thread.start()
    gateway = f'http://127.0.0.1:{proxy.server_port}/chat/completions'
    env['NO_PROXY'] = env.get('NO_PROXY', '') + ',127.0.0.1,localhost'
    env['no_proxy'] = env.get('no_proxy', '') + ',127.0.0.1,localhost'
bot = subprocess.Popen([
    str(root / 'install/rmtc_bot/lib/rmtc_bot/contestant'), '--ros-args',
    '-p', 'ai_provider:=deepseek', '-p', f'ai_gateway:={gateway}',
    '-p', 'ai_model:=deepseek-flash', '-p', 'ai_budget:=4', '-p', 'max_ticks:=250',
    '-p', f'log_dir:={out}'
], env=env, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
env.pop('DEEPSEEK_API_KEY', None)
rclpy.init()
node = rclpy.create_node('deepseek_wire_test')
qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.VOLATILE)
pub = node.create_publisher(String, '/team_A/perception', qos)
sent, received = {}, {}
def action(msg):
    value = json.loads(msg.data)
    if value['session_id'] == 'deepseek-wire':
        received.setdefault(value['tick'], time.monotonic())
sub = node.create_subscription(String, '/team_A/action', action, qos)
def spin_until(deadline):
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=min(.005, max(0, deadline - time.monotonic())))
try:
    deadline = time.monotonic() + 8
    while (pub.get_subscription_count() == 0 or node.count_publishers('/team_A/action') == 0) and time.monotonic() < deadline:
        spin_until(time.monotonic() + .05)
    assert pub.get_subscription_count() == 1, 'node did not subscribe'
    assert node.count_publishers('/team_A/action') == 1, 'action publisher did not appear'
    start = time.monotonic()
    count = 200 if args.schema_repair_fault else 120
    for tick in range(count):
        frame = {'session_id': 'deepseek-wire', 'tick': tick, 'vision_radius': 4,
                 'map': {'width': 24, 'height': 24},
                 'self': {'team': 'A', 'position': [1, 1] if args.schema_repair_fault else [8, 8], 'hp': 100, 'alive': True,
                          'protected': False, 'last_action': None},
                 'visible_entities': [], 'score': {'resource_value': 0, 'effective_damage': 0,
                                                  'mission_completed_ids': []}, 'missions': [],
                 'event_text': '预警：Tick 150时，矩形(3,4)到(7,8)发生一次爆炸，伤害80。'}
        if args.schema_repair_fault:
            frame['event_text'] = (cleanup_notice if tick >= 99 else
                '预告：Tick 80，以(8,8)为交点生成十字障碍，持续20 Tick，Tick 100移除临时障碍。')
        sent[tick] = time.monotonic()
        pub.publish(String(data=json.dumps(frame, ensure_ascii=False)))
        spin_until(start + (tick + 1) * .1)
    spin_until(time.monotonic() + .2)
    rows = [json.loads(line) for path in out.glob('*.jsonl') for line in path.read_text().splitlines()]
    ai = [row for row in rows if row['kind'] == 'ai']
    decisions = [row for row in rows if row['kind'] == 'decision']
    latencies = sorted((received[tick] - when) * 1000 for tick, when in sent.items() if tick in received)
    local = sorted(row['latency_ms'] for row in decisions)
    understood = any(row['accepted'] and any(event['kind'] == 'explosion' and event['start_tick'] == 150
                     and event['rect'] == [[3, 4], [7, 8]] for event in row['event_memory']['events']) for row in ai)
    if args.schema_repair_fault:
        understood = any(row['accepted'] and any(event['kind'] == 'world_split' and event['start_tick'] == 80
            and event['end_tick'] == 100 and event['points'] == [[8, 8]]
            for event in row['event_memory']['events']) for row in ai)
        repaired = [row for row in ai if row.get('repair_attempt') and row['accepted']]
        repair_verified = any(len([e for e in row['event_memory']['events'] if e['kind'] == 'world_split']) == 1
            and any(e['kind'] == 'world_split' and e['start_tick'] == 80 and e['end_tick'] == 100 and e['points'] == [[8, 8]]
                    for e in row['event_memory']['events']) for row in repaired)
    summary = {'synthetic_perception': True, 'ticks': len(sent), 'actions': len(latencies),
               'tick_ms': 100, 'action_roundtrip_p95_ms': latencies[int(len(latencies) * .95)] if latencies else None,
               'action_roundtrip_max_ms': max(latencies, default=0),
               'decision_p95_ms': local[int(len(local) * .95)] if local else None,
               'decision_max_ms': max(local, default=0), 'event_understood': understood,
               'ai': [{key: row[key] for key in ['http_status', 'accepted', 'elapsed_ms', 'request_tick',
                        'current_tick', 'reasoning_chars', 'usage', 'error']} for row in ai],
               'log_dir': str(out)}
    summary['passed'] = len(latencies) == count and understood and all(row['reasoning_chars'] == 0 for row in ai)
    if args.schema_repair_fault:
        summary.update(schema_fault_injection=True, injected_responses=faults, real_upstream_calls=upstream_calls,
            repair_verified=repair_verified, repaired_outputs=len(repaired))
        summary['passed'] = summary['passed'] and faults == 1 and upstream_calls == 3 and repair_verified
    (out / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2))
    print(json.dumps(summary, ensure_ascii=False), flush=True)
    assert summary['passed'], 'real API/ROS pipeline test failed'
finally:
    node.destroy_node()
    rclpy.shutdown()
    if bot.poll() is None:
        os.killpg(bot.pid, signal.SIGINT)
        try:
            bot.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(bot.pid, signal.SIGKILL)
            bot.wait()
    stream.close()
    if proxy:
        proxy.shutdown()
        proxy.server_close()
        proxy_thread.join()
