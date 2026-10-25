"""Independent topology and audit-failure checks for the stage-one test harness."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from stage1_eval import generate
from stage1_suite_audit import audit
from stage1_joint_dev_report import summarize


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class GeneratorTest(unittest.TestCase):
    def test_dense_maps_keep_exact_density_connected_inventory_and_reproducibility(self):
        template = {'stage': 1, 'map': {'width': 20, 'height': 20, 'resources': [
            {'id': str(i), 'value': 10 if i < 12 else 20} for i in range(16)]}}
        for ratio in [.25, .30, .35, .40]:
            config = generate(template, 73021, ratio)
            self.assertEqual(config, generate(template, 73021, ratio))
            blocked = set(map(tuple, config['map']['obstacles']))
            self.assertEqual(len(blocked), round(400 * ratio))
            spawn = tuple(config['robots'][0]['spawn'])
            reached, pending = {spawn}, [spawn]
            while pending:
                x, y = pending.pop()
                for dx, dy in [(0, -1), (1, 0), (0, 1), (-1, 0)]:
                    q = x + dx, y + dy
                    if 0 <= q[0] < 20 and 0 <= q[1] < 20 and q not in blocked and q not in reached:
                        reached.add(q)
                        pending.append(q)
            self.assertEqual(len(reached), 400 - len(blocked))
            resources = config['map']['resources']
            self.assertEqual(sum(r['value'] for r in resources), 200)
            positions = {tuple(r['position']) for r in resources}
            self.assertEqual(len(positions), 16)
            self.assertTrue(positions <= reached - {spawn})


class AuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.suite = Path(self.temporary.name)
        (self.suite / 'contestant').write_bytes(b'fixture binary')
        (self.suite / 'stage1_eval.py').write_text('# fixture evaluator\n')
        config = {'stage': 1, 'map': {'width': 20, 'height': 20}, 'robots': [{'team': 'A'}]}
        fixture = self.suite / 'fixture.json'
        write(fixture, config)
        case = dict(case_id='case', group='known25', seed=1, config_file=str(fixture), config_sha256=digest(fixture))
        self.manifest = dict(status='complete', expected_games=2, cases=[case],
            binary_sha256=digest(self.suite / 'contestant'), evaluator_sha256=digest(self.suite / 'stage1_eval.py'))
        write(self.suite / 'manifest.json', self.manifest)
        results = []
        for arm in ['control', 'candidate']:
            path = self.suite / arm
            path.mkdir()
            write(path / 'config.json', config)
            (path / 'contestant').write_bytes((self.suite / 'contestant').read_bytes())
            (path / 'stage1_eval.py').write_bytes((self.suite / 'stage1_eval.py').read_bytes())
            write(path / 'run.json', dict(stage=1, tick_ms=100, max_ticks=100, ai=False,
                judge_manifest_sha256='judge', judge_core_source_sha256='core', config_sha256=digest(path / 'config.json')))
            write(path / 'parameters.json', dict(team='A', stage=1, stage1_resource_route=arm == 'candidate'))
            write(path / 'interfaces.json', dict(subscriptions=['/team_A/perception', '/referee/mission',
                '/referee/event_text', '/referee/team_registry'], publishers=['/team_A/action', '/team_A/registration']))
            increments = [20] * 4 + [10] * (12 if arm == 'candidate' else 11)
            value = sum(increments)
            board = dict(team='A', score=.2 * value, resource_value=value, invalid_actions=0)
            write(path / 'summary.json', dict(own=board, p95_ms=1, max_ms=2, resource_by_tick={}))
            write(path / 'state.json', dict(board=dict(teams=[board]), checks={'A': {'last_action': {'tick': 99, 'action': 'wait'}}}))
            frames = [dict(kind='decision', tick=t, session_id=arm, resource_value=sum(increments[:t]),
                action={'action': 'wait'}, last_action=None if t == 0 else dict(tick=t - 1, action='wait')) for t in range(100)]
            frames.append(dict(kind='final', resource_value=value))
            (path / 'own').mkdir()
            (path / 'own' / 'own.jsonl').write_text(''.join(json.dumps(f) + '\n' for f in frames))
            results.append(dict(case_id='case', group='known25', seed=1, arm=arm, output=str(path)))
        write(self.suite / 'results.json', results)

    def change(self, relative, mutate):
        path = self.suite / relative
        data = json.loads(path.read_text())
        mutate(data)
        write(path, data)

    def test_complete_cohort_passes_and_reconstructs_counts(self):
        result = audit(self.suite)
        self.assertTrue(result['stage1_gates_passed'])
        self.assertEqual(result['decisions'], 200)
        self.assertEqual(result['groups']['known25']['candidate']['high'], 4)
        self.assertEqual(result['groups']['known25']['candidate']['normal'], 12)

    def test_rejects_final_receipt_mismatch(self):
        self.change('candidate/state.json', lambda d: d['checks']['A']['last_action'].update(tick=98))
        with self.assertRaisesRegex(ValueError, 'Final action99'):
            audit(self.suite)

    def test_rejects_foreign_perception_subscription(self):
        self.change('candidate/interfaces.json', lambda d: d['subscriptions'].append('/team_B/perception'))
        with self.assertRaisesRegex(ValueError, 'Illegal subscription'):
            audit(self.suite)

    def test_rejects_unapplied_runtime_flag(self):
        self.change('candidate/parameters.json', lambda d: d.update(stage1_resource_route=False))
        with self.assertRaisesRegex(ValueError, 'Actual parameters'):
            audit(self.suite)

    def test_additional_joint_switch_is_audited_in_both_arms(self):
        self.change('manifest.json', lambda d: d.update(candidate_params=['stage1_joint_coverage:=true']))
        for arm in ['control', 'candidate']:
            self.change(arm + '/parameters.json', lambda d, arm=arm: d.update(stage1_joint_coverage=arm == 'candidate'))
        self.assertTrue(audit(self.suite)['stage1_gates_passed'])
        self.change('candidate/parameters.json', lambda d: d.update(stage1_joint_coverage=False))
        with self.assertRaisesRegex(ValueError, 'Additional candidate parameter'):
            audit(self.suite)

    def test_subgoal_switch_is_verified_in_both_arms(self):
        self.change('manifest.json', lambda d: d.update(candidate_params=['stage1_joint_coverage:=true','stage1_subgoal_risk:=true']))
        for arm in ['control','candidate']:
            self.change(arm+'/parameters.json',lambda d,arm=arm:d.update(stage1_joint_coverage=arm=='candidate',stage1_subgoal_risk=arm=='candidate'))
        self.assertTrue(audit(self.suite)['stage1_gates_passed'])
        self.change('control/parameters.json',lambda d:d.update(stage1_subgoal_risk=True))
        with self.assertRaisesRegex(ValueError,'Additional candidate parameter'):audit(self.suite)

    def test_rejects_wrong_tick_period(self):
        self.change('candidate/run.json', lambda d: d.update(tick_ms=50))
        with self.assertRaisesRegex(ValueError, 'Wrong competition timing'):
            audit(self.suite)

    def test_rejects_missing_first_decision(self):
        path = self.suite / 'candidate/own/own.jsonl'
        path.write_text('\n'.join(path.read_text().splitlines()[1:]) + '\n')
        with self.assertRaisesRegex(ValueError, 'Incomplete tick'):
            audit(self.suite)

    def test_detects_changed_preregistered_fixture(self):
        self.change('fixture.json', lambda d: d.update(stage=2))
        with self.assertRaisesRegex(ValueError, 'Preregistered fixture changed'):
            audit(self.suite)

    def test_rejects_unfulfilled_certified_resource_promise(self):
        path = self.suite / 'candidate/own/own.jsonl'
        records = [json.loads(line) for line in path.read_text().splitlines()]
        records[1]['stage1_route_evaluation'] = dict(
            distance_model='observed_free_route_with_optimistic_value_bound',
            route=[[1, 1]], planned_steps=1, planned_value=200)
        path.write_text(''.join(json.dumps(r) + '\n' for r in records))
        with self.assertRaisesRegex(ValueError, 'was not retained'):
            audit(self.suite)

    def test_callback_overrun_blocks_promotion(self):
        self.change('candidate/summary.json', lambda d: d.update(max_ms=51))
        self.assertFalse(audit(self.suite)['stage1_gates_passed'])


class SimulationReportTest(unittest.TestCase):
    def test_rejects_reordered_or_wrong_cohort_simulation(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace=Path(temporary)
            write(workspace/'dev-input.json',[dict(seed=1,cohort='dev',group='held25'),dict(seed=2,cohort='dev',group='held25')])
            baseline=[dict(seed=i,score=30,max_ms=1,invalid_actions=0) for i in [1,2]]
            write(workspace/'sim-control-development.json',baseline)
            result=workspace/'candidate.json';write(result,list(reversed(baseline)))
            with self.assertRaisesRegex(ValueError,'order or seed'):summarize(workspace,result)
            candidate=[dict(r,cohort='wrong') for r in baseline];write(result,candidate)
            with self.assertRaisesRegex(ValueError,'cohort/group'):summarize(workspace,result)
            write(result,baseline);report=summarize(workspace,result)
            self.assertEqual(report['games'],2);self.assertEqual(len(report['input_hashes']),3)


if __name__ == '__main__':
    unittest.main()
