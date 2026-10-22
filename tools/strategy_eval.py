"""Development-only, isolated practice-judge evaluation. Never feeds truth to bots.

Run inside a sourced ROS 2 environment. Configurations and final spectator scores
are used only by this test driver; contestants receive public ROS perception.
"""
import argparse
import collections
import copy
import gzip
import hashlib
import itertools
import json
import os
import pathlib
import random
import signal
import shutil
import subprocess
import time
import urllib.error
import urllib.request


def compress_judge_logs(directory):
    """Archive only closed judge traces; configs, results and own logs stay plain."""
    count = 0
    for source in directory.rglob('*.jsonl'):
        target = source.with_suffix(source.suffix + '.gz')
        try:
            with source.open('rb') as incoming, gzip.open(target, 'wb', compresslevel=6) as outgoing:
                shutil.copyfileobj(incoming, outgoing)
        except OSError:
            target.unlink(missing_ok=True)
            raise
        source.unlink()
        count += 1
    return count



def control_tick_coverage(decisions, max_ticks):
    """Require one decision for every expected control Tick, including Tick zero."""
    ticks = [r['tick'] for r in decisions]
    expected, actual = set(range(max_ticks)), set(ticks)
    return len(ticks) == max_ticks and actual == expected, sorted(expected - actual)


def repair_generated_event_geometry(config):
    """Keep generated maps valid without changing walls/resources or bot inputs.

    Only the development judge configuration is adjusted. Event geometry reaches
    the contestant exclusively through the judge's natural-language notices.
    """
    if not config['events'].get('enabled', True):
        return
    width, height = config['map']['width'], config['map']['height']
    blocked = {tuple(p) for p in config['map']['obstacles']}
    reserved = {tuple(r['spawn']) for r in config['robots']}
    reserved.update(tuple(r['position']) for r in config['map']['resources'])
    definitions = config['events']['definitions']
    for kind in ('angel', 'pepper'):
        points = definitions.get(kind, {}).get('points', [])
        claimed = {tuple(p) for p in points if tuple(p) not in blocked
                   and 0 <= p[0] < width and 0 <= p[1] < height}
        for index, point in enumerate(points):
            if tuple(point) in claimed:
                continue
            candidates = [(x, y) for x in range(width) for y in range(height)
                          if (x, y) not in blocked | reserved | claimed]
            if not candidates:
                raise ValueError('No legal event pickup cell in generated map')
            replacement = min(candidates, key=lambda p: (abs(p[0]-point[0])+abs(p[1]-point[1]), p))
            points[index] = list(replacement)
            claimed.add(replacement)
    split = definitions.get('world_split', {})
    point = split.get('point')
    if point:
        spawn_x = {r['spawn'][0] for r in config['robots']}
        spawn_y = {r['spawn'][1] for r in config['robots']}
        if point[0] in spawn_x or point[1] in spawn_y:
            candidates = [(x, y) for x in range(width) for y in range(height)
                          if x not in spawn_x and y not in spawn_y and (x, y) not in blocked]
            if not candidates:
                raise ValueError('No legal world-split cross in generated map')
            split['point'] = list(min(candidates, key=lambda p: (abs(p[0]-point[0])+abs(p[1]-point[1]), p)))


def apply_standard_event_values(config):
    """Use published standard magnitudes; keep test schedule/geometry explicit.

    Practice defaults use weaker/shorter events. This development-only variant
    must not be described as the unknown formal event schedule or map.
    """
    values = {
        'explosion': {'damage':80},
        'illusion': {'effect_ticks':50},
        'angel': {'duration_ticks':20, 'acquire_ticks':20, 'effect_ticks':10, 'heal_per_tick':10},
        'unrivaled': {'duration_ticks':50, 'stay_ticks':10, 'attack_radius':2, 'attack_damage':50, 'defended_damage':10},
        'world_split': {'duration_ticks':30},
        'pepper': {'duration_ticks':30, 'damage':50},
        'purity': {'duration_ticks':20, 'reflection_damage':20},
        'pan': {'duration_ticks':30, 'movement_window_ticks':20, 'vulnerability_ticks':30, 'damage_multiplier':2},
    }
    for kind, parameters in values.items():
        config['events']['definitions'][kind].update(parameters)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--judge-root', type=pathlib.Path, required=True)
    parser.add_argument('--workspace', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument('--binary', type=pathlib.Path)
    parser.add_argument('--label', default='candidate')
    parser.add_argument('--seats', default='A,B,C,D')
    parser.add_argument('--repetitions', type=int, default=1)
    parser.add_argument('--variants', default='default')
    parser.add_argument('--tick-ms', type=int, default=50)
    parser.add_argument('--max-ticks', type=int, default=500)
    parser.add_argument('--port', type=int, default=8083)
    parser.add_argument('--domain', type=int, default=48)
    parser.add_argument('--no-events', action='store_true')
    parser.add_argument('--ai', choices=['off', 'deepseek', 'gateway'], default='off')
    parser.add_argument('--key-file', type=pathlib.Path)
    parser.add_argument('--param', action='append', default=[], help='Planner-only ROS parameter name:=value')
    args = parser.parse_args()
    seats = args.seats.split(',')
    if len(set(seats)) != len(seats) or any(s not in 'ABCD' or len(s) != 1 for s in seats):
        parser.error('--seats requires unique comma-separated seats A,B,C,D')
    if not 1 <= args.repetitions <= 100:
        parser.error('--repetitions must be between 1 and 100')
    allowed_params = {'early_movement_scale', 'early_damage_scale', 'damage_scale_ticks',
                      'resource_paths_exact', 'resource_lookahead',
                      'tactical_forecast', 'forecast_start_tick', 'early_interception', 'frontier_lookahead',
                      'information_paths', 'race_aware_resources', 'resource_beam', 'transient_occupancy', 'event_timing',
                      'ai_action_revision', 'revision_window_ms', 'productive_escape'}
    if any(p.partition(':=')[0] not in allowed_params or ':=' not in p for p in args.param):
        parser.error('--param only accepts the documented planner parameters')
    if args.ai != 'off' and args.tick_ms < 500:
        parser.error('Real AI comparisons require the standard >=500 ms action window')
    if args.key_file and args.ai == 'off':
        parser.error('--key-file requires --ai')
    stamp = str(time.time_ns())
    output = args.workspace / 'test-results' / (args.label + '-' + stamp)
    output.mkdir(parents=True)
    source_binary = args.binary or args.workspace / 'install/rmtc_bot/lib/rmtc_bot/contestant'
    binary = output / 'contestant'
    shutil.copy2(source_binary, binary)
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    (output / 'run.json').write_text(json.dumps(dict(binary_sha256=binary_hash,
        evaluator_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
        source_binary=str(source_binary), ai=args.ai, tick_ms=args.tick_ms,
        max_ticks=args.max_ticks, no_events=args.no_events, variants=args.variants,
        seats=args.seats, repetitions=args.repetitions, domain=args.domain,
        port=args.port, planner_params=args.param), indent=2))
    base = f'http://127.0.0.1:{args.port}'
    environment = dict(os.environ, ROS_DOMAIN_ID=str(args.domain),
                       RMTC_INTERNAL_DOMAIN_ID=str(args.domain + 1), ROS_LOCALHOST_ONLY='0',
                       RMW_IMPLEMENTATION='rmw_fastrtps_cpp')
    # No test key is passed to the judge or reference bots in direct mode.
    environment.pop('DEEPSEEK_API_KEY', None)
    streams, children, results = [], [], []

    def launch(command, name, extra=None):
        stream = (output / (name + '.log')).open('w')
        streams.append(stream)
        process = subprocess.Popen(command, env=dict(environment, **(extra or {})),
                                   stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        children.append(process)
        return process

    def stop(process):
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGINT)
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()

    def api(path, payload=None):
        request = urllib.request.Request(base + path,
            data=None if payload is None else json.dumps(payload).encode(),
            headers={'Content-Type': 'application/json'})
        # Local judge traffic must never go through an external proxy.
        try:
            with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(request, timeout=30) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            # This is our local practice-control API, never an LLM endpoint.
            detail = error.read(16384).decode('utf-8', errors='replace')
            (output / 'judge-api-error.json').write_text(json.dumps(
                {'path': path, 'status': error.code, 'response': detail}, ensure_ascii=False, indent=2))
            raise RuntimeError(f'Local judge rejected {path}: HTTP {error.code}: {detail}') from error

    def wait_state(predicate, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if children[0].poll() is not None:
                raise RuntimeError('Judge process exited; inspect ' + str(output))
            try:
                state = api('/api/state')
            except (OSError, ValueError):
                time.sleep(.2)
                continue
            if state.get('fault'):
                raise RuntimeError(state['fault'])
            if predicate(state):
                return state
            time.sleep(.2)
        raise TimeoutError('Judge did not reach expected state: ' + str(output))

    template = json.loads((args.judge_root / 'config/stage2.json').read_text())

    def scenario(name):
        config = copy.deepcopy(template)
        if args.no_events:
            config['events']['enabled'] = False
        geometry_name = name.removesuffix('-standard') if name.endswith('-standard') else name.removesuffix('-illusion')
        if name.endswith('-standard'):
            apply_standard_event_values(config)
        if geometry_name.startswith(('map', 'interior')):
            interior = geometry_name.startswith('interior')
            prefix = 'interior' if interior else 'map'
            rng = random.Random(int(geometry_name.removeprefix(prefix)))
            width, height = rng.randrange(18, 27), rng.randrange(18, 27)
            config['map']['width'], config['map']['height'] = width, height
            if interior:
                spawns = []
                for _ in range(1000):
                    candidate = [rng.randrange(2, width - 2), rng.randrange(2, height - 2)]
                    if all(abs(candidate[0] - p[0]) + abs(candidate[1] - p[1]) >= min(width, height) // 3
                           for p in spawns):
                        spawns.append(candidate)
                    if len(spawns) == 4:
                        break
                if len(spawns) != 4:
                    raise ValueError('Could not generate separated interior spawns: ' + name)
            else:
                spawns = [[1, 1], [width - 2, 1], [1, height - 2], [width - 2, height - 2]]
            rng.shuffle(spawns)
            for robot, spawn in zip(config['robots'], spawns):
                robot['spawn'] = spawn
            reserved = {tuple(p) for p in spawns}
            cells = [(x, y) for x in range(width) for y in range(height) if (x, y) not in reserved]
            rng.shuffle(cells)
            blocked = set()
            def connected(candidate):
                seen, pending = {tuple(spawns[0])}, [tuple(spawns[0])]
                while pending:
                    x, y = pending.pop()
                    for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                        q = x + dx, y + dy
                        if 0 <= q[0] < width and 0 <= q[1] < height and q not in seen and q not in candidate:
                            seen.add(q)
                            pending.append(q)
                return len(seen) == width * height - len(candidate)
            for cell in cells:
                if len(blocked) >= width * height // 10:
                    break
                if connected(blocked | {cell}):
                    blocked.add(cell)
            config['map']['obstacles'] = [list(p) for p in sorted(blocked)]
            free = [p for p in cells if p not in blocked]
            positions = rng.sample(free, len(config['map']['resources']) + 1)
            for resource, position in zip(config['map']['resources'], positions):
                resource['position'] = list(position)
            for mission in config['missions']:
                if mission['type'] == 'reach':
                    mission['position'] = list(positions[-1])
            config['mission_text'] = '练习任务：收集两个普通资源、一个高价值资源，并到达 ' + str(list(positions[-1])) + '。'
        elif geometry_name.startswith('shuffle'):
            rng = random.Random(int(geometry_name.removeprefix('shuffle')))
            width, height = config['map']['width'], config['map']['height']
            blocked = {tuple(p) for p in config['map']['obstacles']}
            blocked.update(tuple(r['spawn']) for r in config['robots'])
            cells = [(x, y) for x in range(1, width - 1) for y in range(1, height - 1)
                     if (x, y) not in blocked]
            positions = rng.sample(cells, len(config['map']['resources']))
            for resource, position in zip(config['map']['resources'], positions):
                resource['position'] = list(position)
        elif geometry_name != 'default':
            raise ValueError('Unknown scenario ' + name)
        if geometry_name != 'default':
            repair_generated_event_geometry(config)
        if name.endswith('-illusion'):
            # Explicit stress scenario: a public, early, map-wide reversal. The
            # same unseen configuration is used for AI-on and AI-off matches.
            events = config['events']
            events['announcement_delay_ticks'] = 12
            events['announcement_count'] = 1
            events['schedule'] = [{'id': 'early_reversal', 'event': 'illusion', 'announce_tick': 1}]
            illusion = events['definitions']['illusion']
            illusion['effect_ticks'] = 40
            illusion['rect'] = {'top_left': [0, 0], 'bottom_right':
                               [config['map']['width'] - 1, config['map']['height'] - 1]}
        return config

    try:
        launch(['/usr/bin/python3', str(args.judge_root / 'practice.py'), '--stage', '2',
                '--port', str(args.port), '--data-dir', str(output / 'judge')], 'judge')
        wait_state(lambda s: s.get('status', {}).get('ready'), 30)
        for variant in args.variants.split(','):
            for seat, repetition in itertools.product(seats, range(1, args.repetitions + 1)):
                label = variant + '-' + seat
                if args.repetitions > 1:
                    label += '-r' + str(repetition)
                api('/api/start', {'stage': 2, 'tick_ms': args.tick_ms,
                    'max_ticks': args.max_ticks, 'bots': [s for s in 'ABCD' if s != seat],
                    'config': scenario(variant)})
                wait_state(lambda s: s.get('status', {}).get('ready'), 25)
                extra = {'RMTC_TEAM_TOKEN': 'practice-' + seat}
                params = ['team:=' + seat, 'team_name:=candidate-' + seat, 'stage:=2',
                          'max_ticks:=' + str(args.max_ticks), 'log_dir:=' + str(output / label),
                          'ai_enabled:=' + ('false' if args.ai == 'off' else 'true')]
                params += args.param
                if args.ai == 'deepseek':
                    params += ['ai_provider:=deepseek', 'ai_gateway:=https://api.deepseek.com/chat/completions',
                               'ai_model:=deepseek-flash']
                    if args.key_file:
                        params += ['token_file:=' + str(args.key_file)]
                    else:
                        extra['DEEPSEEK_API_KEY'] = os.environ.get('DEEPSEEK_API_KEY', '')
                        if not extra['DEEPSEEK_API_KEY']:
                            raise RuntimeError('No DeepSeek credential available')
                elif args.ai == 'gateway':
                    params += ['ai_gateway:=' + base + '/v1/chat/completions']
                command = [str(binary), '--ros-args']
                for param in params:
                    command += ['-p', param]
                bot = launch(command, label, extra)
                time.sleep(1.5)
                if bot.poll() is not None:
                    raise RuntimeError('Contestant exited before match')
                api('/api/resume', {})
                state = wait_state(lambda s: s.get('status', {}).get('finished'),
                                   args.max_ticks * args.tick_ms / 1000 + 40)
                # Give the scoring/diagnostic topics a chance to deliver final totals.
                time.sleep(.4)
                state = api('/api/state')
                stop(bot)
                (output / (label + '-state.json')).write_text(json.dumps(state, ensure_ascii=False, indent=2))
                rows = []
                for logfile in (output / label).glob('*.jsonl'):
                    rows += [json.loads(line) for line in logfile.read_text().splitlines()]
                decisions = [r for r in rows if r['kind'] == 'decision']
                frames_complete, missing_ticks = control_tick_coverage(decisions, args.max_ticks)
                teams = state['board']['teams']
                own = next(t for t in teams if t['team'] == seat)
                def rank_key(team):
                    return (round(team['score'], 9), team.get('mission_completed', 0),
                            round(team.get('resource_value', 0), 9), round(team.get('effective_damage', 0), 9),
                            -team.get('invalid_actions', 0))
                own_key = rank_key(own)
                latencies = sorted(r['latency_ms'] for r in decisions)
                result = dict(variant=variant, seat=seat, repetition=repetition, ai=args.ai, tick_ms=args.tick_ms,
                    planner_params=args.param,
                    binary_sha256=binary_hash, session_id=state.get('session_id'),
                    rank=1 + sum(rank_key(t) > own_key for t in teams),
                    strict_first=all(own_key > rank_key(t) for t in teams if t['team'] != seat),
                    own=own, teams=teams, decisions=len(decisions),
                    action_revisions=sum(r['kind'] == 'action_revision' for r in rows),
                    frames_complete=frames_complete, missing_ticks=missing_ticks,
                    actions=dict(collections.Counter(r['action']['action'] for r in decisions)),
                    reasons=dict(collections.Counter(r['reason'] for r in decisions)),
                    p95_ms=latencies[int(.95 * (len(latencies) - 1))] if latencies else None,
                    max_ms=max(latencies, default=0),
                    accepted_ai=sum(r['kind'] == 'ai' and r.get('accepted', False) for r in rows),
                    rejected_frames=sum(r['kind'] == 'perception_rejected' for r in rows))
                results.append(result)
                (output / 'summary.json').write_text(json.dumps(results, ensure_ascii=False, indent=2))
                print(json.dumps(result, ensure_ascii=False), flush=True)
                if not frames_complete or own.get('invalid_actions', 0):
                    raise RuntimeError('Invalid evaluation: missing control frames or illegal actions')
        print(json.dumps({'output': str(output), 'matches': len(results),
                          'strict_first': sum(r['strict_first'] for r in results)}), flush=True)
    finally:
        for child in reversed(children):
            stop(child)
        for stream in streams:
            stream.close()
    # Runs only after successful evaluation and child shutdown. Never compress
    # an active trace, and retain failed-run logs verbatim for diagnosis.
    archived = compress_judge_logs(output / 'judge')
    print(json.dumps({'archived_judge_jsonl': archived}), flush=True)


if __name__ == '__main__':
    main()
