#!/usr/bin/env python3
"""Offline diagnostics from own decision logs; never reads judge trajectories.

Pass evaluation batch directories. Output is JSON on stdout. Repeated sessions
and incomplete tick coverage are rejected; turnback windows can overlap and
are diagnostics, not a count of provably avoidable ticks.
"""
import argparse
import collections
import json
from pathlib import Path
import statistics


def audit(batches):
    cases, sessions, event_leads = [], set(), []
    for batch in batches:
        run = json.loads((batch / "run.json").read_text())
        summaries = {r["session_id"]: r for r in json.loads((batch / "summary.json").read_text())}
        for path in sorted(batch.glob("*/*.jsonl")):
            rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
            frames = [r for r in rows if r.get("kind") == "decision"]
            if not frames:
                continue
            session = frames[0]["session_id"]
            if session in sessions:
                raise ValueError(f"Duplicate session: {path}")
            sessions.add(session)
            if any(r["session_id"] != session for r in frames):
                raise ValueError(f"Mixed sessions: {path}")
            if [r["tick"] for r in frames] != list(range(run["max_ticks"])):
                raise ValueError(f"Incomplete or duplicate ticks: {path}")
            summary = summaries[session]
            revisions = [r for r in rows if r.get("kind") == "action_revision"]
            cap = next((r["tick"] for r in frames if r["effective_damage"] >= 300), None)
            reach = next((r["tick"] for r in frames if "reach_target" in r["completed"]), None)
            gains = [r["tick"] for prev, r in zip(frames, frames[1:]) if r["resource_value"] > prev["resource_value"]]
            turnbacks, clear = [], []
            for a, b, c in zip(frames, frames[1:], frames[2:]):
                stable = all(a[k] == c[k] for k in ("resource_value", "effective_damage", "completed", "explored_cells"))
                if (a["position"] == c["position"] != b["position"] and stable
                        and a["action"]["action"] == b["action"]["action"] == "move"):
                    turnbacks.append(a["tick"])
                    if not any(r["observed_enemies"] for r in (a, b, c)) and a["hp"] == b["hp"] == c["hp"]:
                        clear.append({"tick": a["tick"], "position": a["position"],
                                      "reason": a["reason"], "target": a.get("target"),
                                      "same_target": a.get("target") == b.get("target")})
            failed = collections.Counter()
            for r in frames:
                receipt = r.get("last_action") or {}
                if receipt.get("action") == "move" and receipt.get("moved") is False:
                    failed[receipt.get("error", "unspecified")] += 1
            first_events = {}
            for row in rows:
                if row.get("kind") == "ai" and row.get("accepted"):
                    for event in row.get("event_memory", {}).get("events", []):
                        key = json.dumps(event, sort_keys=True)
                        first_events.setdefault(key, {"kind": event["kind"], "start": event["start_tick"],
                                                      "known_tick": row["current_tick"],
                                                      "lead": event["start_tick"] - row["current_tick"]})
            event_leads.extend({"case": path.parent.name, **e} for e in first_events.values())
            cases.append({"case": path.parent.name, "log": str(path), "binary_sha256": run["binary_sha256"],
                          "planner_params": run.get("planner_params", []), "own": summary["own"],
                          "completed": frames[-1]["completed"], "last_resource_tick": max(gains, default=None),
                          "action_revisions": len(revisions),
                          "damage_cap_tick": cap, "reach_tick": reach,
                          "actions": dict(collections.Counter(r["action"]["action"] for r in frames)),
                          "after_cap": dict(collections.Counter(r["action"]["action"] for r in frames if r["effective_damage"] >= 300)),
                          "postcap_attacks_known_resources": sum(r["effective_damage"] >= 300 and r["action"]["action"] == "attack" and r["known_resources"] > 0 for r in frames),
                          "turnbacks": len(turnbacks), "clear_turnbacks": clear, "failed_moves": dict(failed)})
    total = {"matches": len(cases), "damage_cap_matches": sum(c["own"]["effective_damage"] >= 300 for c in cases),
             "resource_cap_matches": sum(c["own"]["resource_value"] >= 120 for c in cases),
             "reach_completed_matches": sum(c["reach_tick"] is not None for c in cases),
             "three_tasks_matches": sum(len(c["completed"]) >= 3 for c in cases)}
    for field in ("last_resource_tick", "damage_cap_tick", "reach_tick"):
        values = [c[field] for c in cases if c[field] is not None]
        total[field + "_median"] = statistics.median(values) if values else None
    for field in ("actions", "after_cap", "failed_moves"):
        counts = collections.Counter()
        for c in cases:
            counts.update(c[field])
        total[field] = dict(counts)
    total["turnbacks"] = sum(c["turnbacks"] for c in cases)
    total["action_revisions"] = sum(c["action_revisions"] for c in cases)
    clear = [w for c in cases for w in c["clear_turnbacks"]]
    total.update(clear_turnbacks=len(clear), clear_turnbacks_before_100=sum(w["tick"] < 100 for w in clear),
                 clear_turnbacks_same_target=sum(w["same_target"] for w in clear),
                 postcap_attacks_known_resources=sum(c["postcap_attacks_known_resources"] for c in cases))
    return {"note": "Own observations only; action counts refer to initial decisions, revisions are reported separately and may be ignored if late. Overlapping turnbacks are diagnostic windows, not wasted-tick totals. Practice task points do not establish formal AI qualification.",
            "totals": total, "cases": cases, "event_first_known": event_leads}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batches", nargs="+", type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.batches), ensure_ascii=False, indent=2))
