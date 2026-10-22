"""Local synthetic ROS/HTTP timing regression; no external model or judge.

The HTTP server supplies explicit test fixtures, not an event parser. This
checks transport/action timing only and provides no AI qualification evidence.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--domain', type=int, default=88)
    args = parser.parse_args()
    os.environ.update(ROS_DOMAIN_ID=str(args.domain), ROS_LOCALHOST_ONLY='0',
                      RMW_IMPLEMENTATION='rmw_fastrtps_cpp')
    import rclpy
    from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
    from std_msgs.msg import String

    gates = {name: threading.Event() for name in ('within', 'late', 'retired', 'finished')}
    requested = set()
    served = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *unused):
            pass

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            public = json.loads(body['messages'][1]['content'])
            session = public['session_id']
            events = []
            if public['source'] != 'mission' and session in gates:
                requested.add(session)
                if not gates[session].wait(8):
                    self.send_error(504)
                    return
                events = [{'kind': 'explosion', 'start_tick': 11, 'end_tick': 12,
                           'rect': [[8, 8], [8, 8]], 'params': {'damage': 100}}]
                if public['source'] == 'topic':
                    # A second distinct model update on the same Tick.
                    events.append({'kind': 'explosion', 'start_tick': 11, 'end_tick': 12,
                                   'rect': [[7, 7], [9, 9]], 'params': {'damage': 100}})
            output = {'session_id': session, 'events': events, 'effects': []}
            payload = json.dumps({'choices': [{'finish_reason': 'stop',
                                 'message': {'content': json.dumps(output)}}]}).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            served.append((session, public['source']))

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    root = Path(__file__).resolve().parents[1]
    out = root / 'test-results' / f'action-revision-wire-{time.time_ns()}'
    out.mkdir(parents=True)
    stream = (out / 'process.log').open('w')
    env = dict(os.environ, RMTC_TEAM_TOKEN='local-fixture-only')
    for name in ('NO_PROXY', 'no_proxy'):
        env[name] = env.get(name, '') + ',127.0.0.1,localhost'
    command = [str(root / 'install/rmtc_bot/lib/rmtc_bot/contestant'), '--ros-args']
    for parameter in ('ai_provider:=gateway', f'ai_gateway:=http://127.0.0.1:{server.server_port}/v1/chat/completions',
                      'ai_model:=fixture', 'ai_action_revision:=true', 'revision_window_ms:=350',
                      f'log_dir:={out}'):
        command += ['-p', parameter]
    bot = subprocess.Popen(command, env=env, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
    rclpy.init()
    node = rclpy.create_node('action_revision_wire_test')
    qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE,
                     durability=DurabilityPolicy.VOLATILE)
    pub = node.create_publisher(String, '/team_A/perception', qos)
    notices = node.create_publisher(String, '/referee/event_text',
        QoSProfile(depth=128, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.TRANSIENT_LOCAL))
    actions = []
    subscription = node.create_subscription(String, '/team_A/action', lambda msg: actions.append(json.loads(msg.data)), qos)

    def spin(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            rclpy.spin_once(node, timeout_sec=min(.005, max(0, end-time.monotonic())))

    def until(predicate, timeout=3):
        end = time.monotonic() + timeout
        while not predicate() and time.monotonic() < end:
            spin(.005)
        assert predicate(), 'timed out awaiting fixture/action'

    def frame(session, tick=10):
        grid = '\n'.join(''.join('1' if (x,y)==(4,4) else '2' if (x,y)==(5,4) else '0'
                                 for x in range(9)) for y in range(9))
        return {'session_id': session, 'tick': tick, 'vision_radius': 4, 'map': {'width':24,'height':24},
                'self': {'team':'A','position':[8,8],'hp':100,'alive':True,'protected':False,'last_action':None},
                'visible_entities': [{'type':'robot','team':'B','id':'B','position':grid,
                                      'hp':100,'defending':False,'protected':False}],
                'score': {'resource_value':0,'effective_damage':0,'mission_completed_ids':[]},
                'missions':[], 'event_text':'Synthetic event fixture.'}

    def send(session, tick=10):
        pub.publish(String(data=json.dumps(frame(session, tick))))

    def begin(session):
        before = len(actions)
        send(session)
        until(lambda: len(actions)>before and session in requested)
        assert actions[-1]['action']=='attack', actions[-1]
        return len(actions)

    try:
        until(lambda: pub.get_subscription_count()==1 and node.count_publishers('/team_A/action')==1, 8)
        count = begin('within')
        gates['within'].set()
        until(lambda: len(actions)>count, .3)
        revised = actions[-1]
        assert revised['tick']==10 and revised['session_id']=='within' and revised['action']=='move', revised
        count = len(actions)
        notices.publish(String(data='Second synthetic event fixture.'))
        until(lambda: ('within', 'topic') in served)
        spin(.08)
        assert len(actions)==count, 'more than one revision in same Tick'
        send('within')
        until(lambda: len(actions)>count)
        assert actions[-1]==revised, 'duplicate frame restored stale candidate'

        count = begin('late')
        spin(.4)
        gates['late'].set()
        until(lambda: ('late', 'perception') in served)
        spin(.08)
        assert len(actions)==count, 'late result revised stale action window'

        begin('retired')
        send('fresh', 0)
        until(lambda: actions[-1]['session_id']=='fresh')
        count = len(actions)
        gates['retired'].set()
        until(lambda: ('retired', 'perception') in served)
        spin(.1)
        assert len(actions)==count, 'retired session result produced an action'

        count = begin('finished')
        send('finished', 500)
        spin(.05)
        gates['finished'].set()
        until(lambda: ('finished', 'perception') in served)
        spin(.1)
        assert len(actions)==count, 'finished match produced an action'
        assert bot.poll() is None, 'contestant crashed'
        rows = [json.loads(line) for path in out.glob('*.jsonl') for line in path.read_text().splitlines()]
        revisions = [r for r in rows if r['kind']=='action_revision']
        assert len(revisions)==1 and revisions[0]['session_id']=='within'
        assert revisions[0]['frame_age_ms']<350
        result = {'passed':True, 'checks':['same-Tick correction','at most one revision',
                  'duplicate replays corrected action','late result ignored','retired result ignored',
                  'finished match ignored'], 'actions':len(actions), 'revisions':len(revisions),
                  'revision_age_ms':revisions[0]['frame_age_ms'], 'output':str(out)}
        (out/'summary.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result))
    finally:
        for gate in gates.values():
            gate.set()
        node.destroy_node()
        rclpy.shutdown()
        if bot.poll() is None:
            os.killpg(bot.pid, signal.SIGINT)
            try:
                bot.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(bot.pid, signal.SIGKILL)
                bot.wait()
        server.shutdown()
        server.server_close()
        stream.close()


if __name__ == '__main__':
    main()
