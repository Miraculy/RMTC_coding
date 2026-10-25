"""Offline report figure. Full fixtures are never passed to a contestant."""
import argparse
import json
from pathlib import Path

from html import escape


def load_run(suite, arm, seed, workspace):
    rows = json.loads((suite / 'results.json').read_text())
    row = next(r for r in rows if r['seed'] == seed and r['arm'] == arm)
    output = Path(row['output'].replace('/root/ros2_ws', str(workspace)))
    records = [json.loads(line) for p in (output / 'own').glob('*.jsonl') for line in p.read_text().splitlines()]
    frames = [r for r in records if r.get('kind') == 'decision']
    if [f['tick'] for f in frames] != list(range(100)):
        raise ValueError('Figure requires a complete actual game')
    state = json.loads((output / 'state.json').read_text())
    end = state['checks']['A']['position']
    captured = {tuple(b['position']) for a, b in zip(frames, frames[1:]) if b['resource_value'] > a['resource_value']}
    if row['summary']['own']['resource_value'] > frames[-1]['resource_value']:
        captured.add(tuple(end))
    return row, frames, end, captured


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--stable-suite', type=Path, required=True)
    parser.add_argument('--optimistic-suite', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=20261024)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    example = load_run(args.stable_suite, 'control', args.seed, args.workspace)[0]
    config = json.loads((Path(example['output'].replace('/root/ros2_ws', str(args.workspace))) / 'config.json').read_text())
    scale=18;top=65;panel=390;left=28
    svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="515" viewBox="0 0 1200 515">',
         '<rect width="1200" height="515" fill="white"/>',
         '<g font-family="sans-serif" font-size="12" fill="#24262c">',
         f'<text x="600" y="21" text-anchor="middle" font-size="16">Seed {args.seed}: actual 100-Tick referee routes; 25% obstacles</text>']
    for index,(suite,arm,name) in enumerate([(args.stable_suite,'control','Formal baseline'),
            (args.optimistic_suite,'candidate','Optimistic candidate (rejected)'),
            (args.stable_suite,'candidate','Stable candidate (experimental)')]):
        row,frames,end,captured=load_run(suite,arm,args.seed,args.workspace)
        offset=left+panel*index
        def point(p):return offset+(p[0]+.5)*scale,top+(p[1]+.5)*scale
        board=row['summary']['own']
        svg.append(f'<text x="{offset+180}" y="43" text-anchor="middle">{escape(name)}: R={board["resource_value"]:.0f}, score={board["score"]:.0f}/40</text>')
        for x,y in config['map']['obstacles']:
            svg.append(f'<rect x="{offset+x*scale}" y="{top+y*scale}" width="18" height="18" fill="#24262c"/>')
        positions=[f['position'] for f in frames]+[end]
        coordinates=' '.join(f'{x},{y}' for x,y in map(point,positions))
        svg.append(f'<polyline points="{coordinates}" fill="none" stroke="#e98013" stroke-width="1.6" stroke-linejoin="round"/>')
        for resource in config['map']['resources']:
            pos=resource['position'];x,y=point(pos);high=resource['value']>=20;color='#cb333a' if high else '#2765ce'
            fill=color if tuple(pos) in captured else 'white'
            if high:
                svg.append(f'<polygon points="{x},{y-4} {x+4},{y} {x},{y+4} {x-4},{y}" fill="{fill}" stroke="{color}" stroke-width="1.2"/>')
            else:svg.append(f'<circle cx="{x}" cy="{y}" r="3.2" fill="{fill}" stroke="{color}" stroke-width="1.2"/>')
        for pos,color in [(positions[0],'#17904a'),(end,'#8837b5')]:
            x,y=point(pos);svg.append(f'<rect x="{x-5}" y="{y-5}" width="10" height="10" fill="none" stroke="{color}" stroke-width="1.7"/>')
        first=next((f for f in frames if (f.get('stage1_route_evaluation') or {}).get('value_certified')),None)
        if first:
            x,y=point(first['position']);tx=x+7 if first['position'][0]<10 else x-180
            svg.append(f'<path d="M{x-4},{y-4}L{x+4},{y+4}M{x-4},{y+4}L{x+4},{y-4}" stroke="#006d6f" stroke-width="2"/>')
            svg.append(f'<rect x="{tx-2}" y="{y+3}" width="176" height="15" fill="white" opacity=".9"/>')
            svg.append(f'<text x="{tx}" y="{y+14}" fill="#006d6f" font-size="10">First certified route: Tick {first["tick"]}</text>')
        svg.append(f'<rect x="{offset}" y="{top}" width="360" height="360" fill="none" stroke="#24262c"/>')
        for v in [0,5,10,15,19]:
            x,y=point([v,v]);svg.append(f'<text x="{x}" y="443" text-anchor="middle" font-size="10">{v}</text>')
            svg.append(f'<text x="{offset-5}" y="{y+3}" text-anchor="end" font-size="10">{v}</text>')
    svg.extend(['<text x="600" y="470" text-anchor="middle">Orange: route | Blue: normal 10 | Red diamond: high 20 | Hollow: uncollected | Green square: start | Purple square: finish</text>',
        '<text x="600" y="493" text-anchor="middle" font-size="11" fill="#555">Offline map display only. The optimistic candidate was rejected for regressions on other maps.</text>','</g></svg>'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text('\n'.join(svg))



if __name__ == '__main__':
    main()
