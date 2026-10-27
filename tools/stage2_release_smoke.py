#!/usr/bin/env python3
"""Check the real release entry/ROS parameters without perception or a match.

Run explicitly after sourcing ROS Humble. Uses only an isolated local ROS domain;
AI is disabled, no perception is sent, and each child is stopped in finally.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile

import rclpy
from rclpy.executors import SingleThreadedExecutor
from rclpy.parameter import parameter_value_to_python
from rcl_interfaces.srv import GetParameters


def main():
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / "stage2/SOURCE_MANIFEST.json").read_text())
    os.environ.update(ROS_DOMAIN_ID="217", ROS_LOCALHOST_ONLY="1",
                      RMW_IMPLEMENTATION="rmw_fastrtps_cpp")
    results = []
    for team, overrides in [("B", {}), ("C", {"resource_blocked_recovery": False, "max_ticks": 321})]:
        expected = dict(manifest["profile"]["parameters"], team=team, stage=2,
                        max_ticks=500, ai_enabled=False)
        expected.update(overrides)
        with tempfile.TemporaryDirectory(prefix="rmtc-release-smoke-") as tmp:
            env = dict(os.environ, RMTC_AI_PROVIDER="gateway", RMTC_TEAM_TOKEN="",
                       RMTC_LOG_DIR=tmp)
            command = [str(root / "run_contestant.sh"), team, "2", "-p", "ai_enabled:=false"]
            for name, value in overrides.items():
                command += ["-p", f"{name}:={str(value).lower()}"]
            with open(Path(tmp) / "process.log", "w+") as log:
                child = subprocess.Popen(command, cwd=tmp, env=env, stdout=log,
                                         stderr=subprocess.STDOUT, start_new_session=True)
                context = rclpy.context.Context()
                node = None
                executor = None
                try:
                    rclpy.init(context=context)
                    executor = SingleThreadedExecutor(context=context)
                    node = rclpy.create_node("stage2_release_inspector", context=context)
                    client = node.create_client(GetParameters, "/rmtc_contestant/get_parameters")
                    if not client.wait_for_service(timeout_sec=15):
                        raise RuntimeError("Release parameter service unavailable")
                    names = list(expected)
                    future = client.call_async(GetParameters.Request(names=names))
                    rclpy.spin_until_future_complete(node, future, executor=executor, timeout_sec=10)
                    if not future.done() or future.result() is None:
                        raise RuntimeError("Parameter response timed out")
                    actual = dict(zip(names, map(parameter_value_to_python, future.result().values)))
                    if actual != expected:
                        raise RuntimeError(f"Parameter mismatch: {actual}")
                    executable = Path(f"/proc/{child.pid}/exe").resolve()
                    if executable != (root / "stage2/install/rmtc_bot/lib/rmtc_bot/contestant").resolve():
                        raise RuntimeError(f"Wrong executable: {executable}")
                    records = [json.loads(line) for p in Path(tmp).glob("*.jsonl")
                               for line in p.read_text().splitlines()]
                    if records:
                        raise RuntimeError("Unexpected records without a match")
                    results.append({"team": team, "actual_parameters": actual,
                                    "executable": "stage2/install/rmtc_bot/lib/rmtc_bot/contestant",
                                    "started_outside_repository": True, "perception_sent": 0,
                                    "ai_enabled": False, "passed": True})
                finally:
                    if executor is not None:
                        executor.shutdown()
                    if node is not None:
                        node.destroy_node()
                    if context.ok():
                        rclpy.shutdown(context=context)
                    if child.poll() is None:
                        os.killpg(child.pid, signal.SIGINT)
                        try:
                            child.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(child.pid, signal.SIGKILL)
                            child.wait(timeout=5)
    print(json.dumps({"cases": results, "matches_started": 0, "passed": True}, indent=2))


if __name__ == "__main__":
    main()
