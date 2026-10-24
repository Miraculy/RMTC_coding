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


def death_recovery(frames):
    """Own observed HP-zero episodes; excludes death with immediate revival.

    A later revival/route is diagnostic evidence only. It is never fed back to
    a contestant, and Manhattan distance here is not a verified travel route.
    """
    episodes = []
    initial_spawn = frames[0].get('initial_spawn', frames[0]['position'])
    for i, (before, after) in enumerate(zip(frames, frames[1:])):
        if not (before['hp'] > 0 and after['hp'] == 0):
            continue
        revival = next((r for r in frames[i+1:] if r['hp'] > 0), None)
        receipt = after.get('last_action') or {}
        cancelled_move = (before['action']['action'] == 'move'
                          and receipt.get('tick') == before['tick']
                          and receipt.get('action') == 'move'
                          and receipt.get('moved') is False
                          and receipt.get('error') == 'E_STATE')
        objective = before.get('target') if before['reason'] in ['resource', 'mission'] else None
        episode = dict(tick=before['tick'], position=before['position'], action=before['action']['action'],
            reason=before['reason'], hp=before['hp'], effective_damage=before['effective_damage'],
            known_resources=before['known_resources'], completed=before['completed'],
            initial_spawn=initial_spawn, target=objective, move_cancelled_by_state=cancelled_move,
            revival_tick=None if revival is None else revival['tick'],
            revival_position=None if revival is None else revival['position'])
        if revival is not None:
            episode['observed_revival_delay'] = revival['tick'] - before['tick']
            episode['fallback_landing'] = initial_spawn is not None and revival['position'] != initial_spawn
            if objective is not None:
                episode['target_manhattan_before_death'] = sum(abs(a-b) for a,b in zip(before['position'], objective))
                episode['target_manhattan_after_revival'] = sum(abs(a-b) for a,b in zip(revival['position'], objective))
        episodes.append(episode)
    return episodes


def audit(batches):
    cases, sessions, event_leads = [], set(), []
    for batch in batches:
        run = json.loads((batch / "run.json").read_text())
        damage_cap = run.get("damage_cap", 300) # Old saved cohorts used the previous rules.
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
            deaths = death_recovery(frames)
            cap = next((r["tick"] for r in frames if r["effective_damage"] >= damage_cap), None)
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
                          "hp_zero_death_episodes": deaths,
                          "damage_cap_tick": cap, "damage_cap": damage_cap, "reach_tick": reach,
                          "damage_decreases": sum(b["effective_damage"] < a["effective_damage"] for a,b in zip(frames,frames[1:])),
                          "cap_losses": sum(a["effective_damage"] >= damage_cap > b["effective_damage"] for a,b in zip(frames,frames[1:])),
                          "actions": dict(collections.Counter(r["action"]["action"] for r in frames)),
                          "after_cap": dict(collections.Counter(r["action"]["action"] for r in frames if r["effective_damage"] >= damage_cap)),
                          "postcap_attacks_known_resources": sum(r["effective_damage"] >= damage_cap and r["action"]["action"] == "attack" and r["known_resources"] > 0 for r in frames),
                          "turnbacks": len(turnbacks), "clear_turnbacks": clear, "failed_moves": dict(failed)})
    total = {"matches": len(cases), "damage_cap_matches": sum(c["own"]["effective_damage"] >= c["damage_cap"] for c in cases),
             "damage_decreases": sum(c["damage_decreases"] for c in cases), "cap_losses": sum(c["cap_losses"] for c in cases),
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
    deaths = [d for c in cases for d in c['hp_zero_death_episodes']]
    total.update(hp_zero_deaths=len(deaths),
                 hp_zero_deaths_with_known_resources=sum(d['known_resources'] > 0 for d in deaths),
                 submitted_moves_cancelled_on_death=sum(d['move_cancelled_by_state'] for d in deaths),
                 observed_revival_delays=dict(collections.Counter(d['observed_revival_delay'] for d in deaths if 'observed_revival_delay' in d)),
                 observed_fallback_landings=sum(d.get('fallback_landing', False) for d in deaths))
    clear = [w for c in cases for w in c["clear_turnbacks"]]
    total.update(clear_turnbacks=len(clear), clear_turnbacks_before_100=sum(w["tick"] < 100 for w in clear),
                 clear_turnbacks_same_target=sum(w["same_target"] for w in clear),
                 postcap_attacks_known_resources=sum(c["postcap_attacks_known_resources"] for c in cases))
    return {"note": "Own observations only; action counts refer to initial decisions, revisions are reported separately and may be ignored if late. HP-zero death episodes exclude immediate revivals; revival delay and Manhattan displacement describe later own observations, not predicted costs or proven avoidable ticks. Overlapping turnbacks are diagnostic windows, not wasted-tick totals. Practice task points do not establish formal AI qualification.",
            "totals": total, "cases": cases, "event_first_known": event_leads}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batches", nargs="+", type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.batches), ensure_ascii=False, indent=2))
