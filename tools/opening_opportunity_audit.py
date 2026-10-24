"""Read only a car's own log to diagnose early high-value opportunities.

Resource increments and visible disappearance cues are observations, not an
opponent score/rank model. No maps, judge traces or other cars' inputs are used.
"""
import argparse
import json
from pathlib import Path


def audit(directory, opening_ticks=60):
    records = [r for p in sorted(directory.glob('*.jsonl'))
               for r in map(json.loads, p.read_text().splitlines())]
    frames = [r for r in records if r.get('kind') == 'decision']
    if not frames or len({r['session_id'] for r in frames}) != 1:
        raise ValueError('Expected one car/session directory')
    seen = [r['tick'] for r in frames if r['tick'] < opening_ticks
            and any(x['value'] >= 20 for x in r.get('observed_resources', []))]
    high_gains, gains, cues = [], [], []
    for previous, current in zip(frames, frames[1:]):
        if current['tick'] != previous['tick'] + 1:
            continue
        delta = current['resource_value'] - previous['resource_value']
        if delta > 0:
            gains.append(dict(tick=current['tick'], value=delta))
        if delta >= 20:
            high_gains.append(current['tick'])
        if not current.get('alive', current['hp'] > 0):
            continue
        current_ids = {r['id'] for r in current.get('observed_resources', [])}
        for resource in previous.get('observed_resources', []):
            position = resource['position']
            if (resource['id'] in current_ids
                    or max(abs(position[i] - current['position'][i]) for i in [0, 1])
                    > current['vision_radius']):
                continue
            for enemy in current.get('observed_enemies', []):
                if enemy['hp'] > 0 and enemy['position'] == position:
                    cues.append(dict(tick=current['tick'], team=enemy['team'],
                                     value=resource['value'], resource_id=resource['id'],
                                     position=position))
    return dict(directory=str(directory), frames=len(frames),
                complete_500=[r['tick'] for r in frames] == list(range(500)),
                first_high_seen_in_opening=min(seen, default=None),
                first_observed_gain_at_least_20=min(high_gains, default=None),
                resource_gains=gains, opponent_collection_cues=cues)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    parser.add_argument('--opening-ticks', type=int, default=60)
    args = parser.parse_args()
    if args.opening_ticks <= 0:
        parser.error('--opening-ticks must be positive')
    rows = [audit(path, args.opening_ticks) for path in args.directories]
    print(json.dumps(dict(note='Observations only; no claim about hidden resource capture or opponent rank.',
        cars=rows, no_high_gain=sum(r['first_observed_gain_at_least_20'] is None for r in rows),
        no_high_seen_in_opening=sum(r['first_high_seen_in_opening'] is None for r in rows),
        cues=sum(len(r['opponent_collection_cues']) for r in rows)), ensure_ascii=False, indent=2))
