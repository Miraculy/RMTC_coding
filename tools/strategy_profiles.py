"""Development-only heterogeneous lineups; never imported by a contestant."""
import hashlib
import itertools
import json
import math
from pathlib import Path
import re
import shutil
import subprocess


def parameter_values(params, allowed):
    if not isinstance(params, list):
        raise ValueError('Profile params must be a list')
    values = {}
    for param in params:
        if not isinstance(param, str) or ':=' not in param:
            raise ValueError('Expected planner name:=value')
        name, raw = param.split(':=', 1)
        if name not in allowed or name in values:
            raise ValueError('Unknown or duplicate planner parameter: ' + name)
        value = json.loads(raw)
        if type(value) not in (bool, int, float) or not math.isfinite(value):
            raise ValueError('Planner parameters must be finite numeric or boolean values')
        values[name] = value
    return values


def read_lineup(path, default_binary, allowed):
    path = Path(path)
    data = json.loads(path.read_text())
    if set(data) != {'schema_version', 'profiles', 'lineup'} or data['schema_version'] != 1:
        raise ValueError('Invalid lineup schema')
    names, definitions = data['lineup'], data['profiles']
    if (not isinstance(names, list) or len(names) != 4 or not all(isinstance(n, str) for n in names)
            or len(set(names)) != 4 or not isinstance(definitions, dict) or set(names) != set(definitions)):
        raise ValueError('Exactly four distinct named profiles are required')
    profiles = {}
    for name in names:
        if not re.fullmatch(r'[a-z][a-z0-9_-]{0,39}', name):
            raise ValueError('Invalid profile name')
        spec = definitions[name]
        if not isinstance(spec, dict) or set(spec) - {'binary', 'params', 'sha256', 'description'}:
            raise ValueError('Unknown profile fields')
        params = spec.get('params', [])
        parameter_values(params, allowed)
        binary = Path(spec.get('binary', default_binary))
        if not binary.is_absolute():
            binary = path.parent / binary
        binary = binary.resolve(strict=True)
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        if 'sha256' in spec and digest != spec['sha256']:
            raise ValueError('Profile binary digest mismatch: ' + name)
        profiles[name] = {'source_binary': str(binary), 'params': params,
                          'binary_sha256': digest, 'description': spec.get('description', '')}
    return {'schema_version': 1, 'lineup': names, 'profiles': profiles}


def freeze_lineup(lineup, output, default_hash):
    """Deduplicate immutable binaries by content, including the legacy default."""
    result = json.loads(json.dumps(lineup))
    for spec in result['profiles'].values():
        digest = spec['binary_sha256']
        filename = 'contestant' if digest == default_hash else 'contestant-' + digest
        destination = output / filename
        if not destination.exists():
            shutil.copy2(spec['source_binary'], destination)
        if hashlib.sha256(destination.read_bytes()).hexdigest() != digest:
            raise ValueError('Frozen profile binary mismatch')
        spec['frozen_binary'] = filename
    return result


def arrangements(names, mode='cyclic', selected=None):
    if mode == 'cyclic':
        # lineup[0] visits A/B/C/D; each other profile keeps its relative order.
        orders = [tuple(names[(i - offset) % 4] for i in range(4)) for offset in range(4)]
    elif mode == 'all':
        orders = list(itertools.permutations(names))
    else:
        raise ValueError('Unknown lineup schedule')
    indexes = list(range(len(orders))) if selected is None else selected
    if (not indexes or len(set(indexes)) != len(indexes)
            or any(type(i) is not int or not 0 <= i < len(orders) for i in indexes)):
        raise ValueError('Invalid or duplicate arrangement indexes')
    return [(i, dict(zip('ABCD', orders[i]))) for i in indexes]


def audit_parameters(environment, assignments, profiles, allowed):
    # Read only each process's actual declared configuration while judge paused.
    # This data stays in the driver and is never sent to any contestant.
    import yaml
    result = {}
    for team, profile in assignments.items():
        output = subprocess.check_output(['ros2', 'param', 'dump', '/rmtc_selfplay_' + team],
                                         env=environment, text=True, timeout=15)
        data = yaml.safe_load(output)
        key = '/rmtc_selfplay_' + team
        actual = data.get(key, data.get(key[1:], {})).get('ros__parameters', {})
        expected = parameter_values(profiles[profile]['params'], allowed)
        if actual.get('team') != team or actual.get('stage') != 2:
            raise ValueError('Wrong seat or stage in running profile')
        for name, value in expected.items():
            if name not in actual or type(actual[name]) is not type(value) or actual[name] != value:
                raise ValueError('Profile parameter not applied: ' + team + '/' + name)
        result[team] = {'profile': profile, 'checked_parameters': expected,
                        'declared_parameters': actual}
    return result


def case_key(row):
    key = (row['variant'], row['seat'], row.get('repetition', 1))
    return key + (row['arrangement'],) if 'arrangement' in row else key
