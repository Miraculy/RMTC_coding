"""Opt-in paid synthetic revision test through the practice AI gateway.

The private upstream key belongs only to the proxy process. Synthetic notices
and deliberately incorrect prior model memory are test fixtures, not match data.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live-gateway', action='store_true', required=True)
    parser.add_argument('--judge-root', type=Path, required=True)
    parser.add_argument('--key-file', type=Path, required=True)
    parser.add_argument('--workspace', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--domain', type=int, default=60)
    parser.add_argument('--port', type=int, default=8558)
    args = parser.parse_args()
    out = args.workspace / 'test-results' / ('event-revision-live-fixture-' + str(time.time_ns()))
    out.mkdir(parents=True)
    binary = out / 'event_revision_benchmark'
    shutil.copy2(args.workspace / 'build/rmtc_bot/event_revision_benchmark', binary)
    environment = dict(os.environ, ROS_DOMAIN_ID=str(args.domain),
                       RMTC_INTERNAL_DOMAIN_ID=str(args.domain + 1), ROS_LOCALHOST_ONLY='0',
                       RMW_IMPLEMENTATION='rmw_fastrtps_cpp')
    for name in ['DEEPSEEK_API_KEY', 'PRACTICE_LLM_KEY', 'RMTC_TEAM_TOKEN']:
        environment.pop(name, None)
    upstream = args.key_file.read_text().strip()
    if not upstream:
        raise ValueError('Empty private gateway credential')
    judge_environment = dict(environment, PRACTICE_LLM_KEY=upstream,
                             PRACTICE_LLM_URL='https://api.deepseek.com/chat/completions',
                             PRACTICE_MODEL='deepseek-flash')
    (out / 'run.json').write_text(json.dumps(dict(synthetic_fixture=True,
        benchmark_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        contestant_sha256=hashlib.sha256((args.workspace / 'install/rmtc_bot/lib/rmtc_bot/contestant').read_bytes()).hexdigest(),
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        domain=args.domain, port=args.port), indent=2))
    (out / 'event_revision_live_test.py').write_bytes(Path(__file__).read_bytes())
    (out / 'event_revision_benchmark.cpp').write_bytes(
        (args.workspace / 'src/rmtc_bot/tools/event_revision_benchmark.cpp').read_bytes())
    base = 'http://127.0.0.1:' + str(args.port)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with (out / 'judge.log').open('w') as log:
        judge = subprocess.Popen(['/usr/bin/python3', str(args.judge_root / 'practice.py'),
            '--stage', '2', '--port', str(args.port), '--data-dir', str(out / 'judge'),
            '--ai-mode', 'real'], env=judge_environment, stdout=log, stderr=subprocess.STDOUT,
            start_new_session=True)
        judge_environment.pop('PRACTICE_LLM_KEY');upstream = None
        try:
            deadline = time.monotonic() + 30
            ready = False
            while time.monotonic() < deadline and judge.poll() is None:
                try:
                    with opener.open(base + '/api/state', timeout=2) as response:
                        ready = json.load(response).get('status', {}).get('ready', False)
                    if ready:
                        break
                except (OSError, ValueError):
                    pass
                time.sleep(.1)
            if not ready:
                raise RuntimeError('Practice proxy did not become ready')
            with (out / 'results.jsonl').open('w') as results:
                run = subprocess.run([str(binary), '--live-gateway', base + '/v1/chat/completions'],
                    env=dict(environment, RMTC_TEAM_TOKEN='practice-A'), stdout=results,
                    stderr=subprocess.STDOUT, timeout=200)
            rows = [json.loads(line) for line in (out / 'results.jsonl').read_text().splitlines()]
            summary = dict(output=str(out), exit_code=run.returncode, result=rows[-1])
            (out / 'summary.json').write_text(json.dumps(summary, indent=2))
            print(json.dumps(summary), flush=True)
            if run.returncode:
                raise RuntimeError('Synthetic live fixture failed; preserve its results')
        finally:
            if judge.poll() is None:
                os.killpg(judge.pid, signal.SIGINT)
                try:
                    judge.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(judge.pid, signal.SIGKILL);judge.wait()


if __name__ == '__main__':
    main()
