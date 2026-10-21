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

parser = argparse.ArgumentParser()
parser.add_argument('--live-deepseek', action='store_true', required=True)
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
bot = subprocess.Popen([
    str(root / 'install/rmtc_bot/lib/rmtc_bot/contestant'), '--ros-args',
    '-p', 'ai_provider:=deepseek', '-p', 'ai_gateway:=https://api.deepseek.com/chat/completions',
    '-p', 'ai_model:=deepseek-flash', '-p', 'ai_budget:=4', '-p', 'max_ticks:=200',
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
    for tick in range(120):
        frame = {'session_id': 'deepseek-wire', 'tick': tick, 'vision_radius': 4,
                 'map': {'width': 24, 'height': 24},
                 'self': {'team': 'A', 'position': [8, 8], 'hp': 100, 'alive': True,
                          'protected': False, 'last_action': None},
                 'visible_entities': [], 'score': {'resource_value': 0, 'effective_damage': 0,
                                                  'mission_completed_ids': []}, 'missions': [],
                 'event_text': '预警：Tick 150时，矩形(3,4)到(7,8)发生一次爆炸，伤害80。'}
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
    summary = {'synthetic_perception': True, 'ticks': len(sent), 'actions': len(latencies),
               'tick_ms': 100, 'action_roundtrip_p95_ms': latencies[int(len(latencies) * .95)] if latencies else None,
               'action_roundtrip_max_ms': max(latencies, default=0),
               'decision_p95_ms': local[int(len(local) * .95)] if local else None,
               'decision_max_ms': max(local, default=0), 'event_understood': understood,
               'ai': [{key: row[key] for key in ['http_status', 'accepted', 'elapsed_ms', 'request_tick',
                        'current_tick', 'reasoning_chars', 'usage', 'error']} for row in ai],
               'log_dir': str(out)}
    summary['passed'] = len(latencies) == 120 and understood and all(row['reasoning_chars'] == 0 for row in ai)
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
