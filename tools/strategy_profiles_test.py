import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from strategy_profiles import arrangements, read_lineup, freeze_lineup, parameter_values, audit_parameters, case_key
from compare_eval import validate_profile_pair
from profile_league_audit import aggregate


class ProfileTest(unittest.TestCase):
    def test_balanced_cyclic_and_full_permutations(self):
        names = ['current', 'collector', 'defensive', 'pressure']
        for mode, count in [('cyclic', 4), ('all', 24)]:
            schedule = arrangements(names, mode)
            self.assertEqual(len(schedule), count)
            self.assertEqual(len({tuple(a.values()) for _, a in schedule}), count)
            for name in names:
                for team in 'ABCD':
                    self.assertEqual(sum(a[team] == name for _, a in schedule), count // 4)
        self.assertEqual(arrangements(names, 'all', [7, 21]), [arrangements(names, 'all')[i] for i in [7, 21]])
        for bad in [[], [0, 0], [-1], [24], [True]]:
            with self.assertRaises(ValueError): arrangements(names, 'all', bad)

    def test_planner_only_parameters(self):
        allowed = {'flag', 'gain'}
        self.assertEqual(parameter_values(['flag:=false', 'gain:=1.5'], allowed), {'flag':False, 'gain':1.5})
        for params in [['team:=B'], ['flag:=true', 'flag:=false'], ['gain:=1e999'], ['gain:="--ros-args"'], ['gain:=[2]'], ['gain:=null']]:
            with self.assertRaises(ValueError): parameter_values(params, allowed)

    def test_input_schema_and_expected_hash(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'bot').write_bytes(b'frozen')
            spec={'schema_version':1,'lineup':['a','b','c','d'],
                  'profiles':{n:{'binary':'bot','params':[]} for n in 'abcd'}}
            path=p/'profiles.json';path.write_text(json.dumps(spec))
            data=read_lineup(path,p/'bot',set())
            self.assertEqual(data['profiles']['a']['source_binary'],str(p/'bot'))
            for change in ['hash','duplicate','unknown']:
                bad=copy.deepcopy(spec)
                if change=='hash':bad['profiles']['a']['sha256']='0'*64
                if change=='duplicate':bad['lineup'][1]='a'
                if change=='unknown':bad['profiles']['a']['remap']='/team_B/perception'
                path.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):read_lineup(path,p/'bot',set())

    def test_freeze_deduplicates_and_detects_tamper(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);source=p/'source';source.write_bytes(b'old');out=p/'out';out.mkdir()
            digest=hashlib.sha256(source.read_bytes()).hexdigest()
            profiles={'lineup':['a','b','c','d'],'profiles':{n:{'source_binary':str(source),'binary_sha256':digest,'params':[]} for n in 'abcd'}}
            frozen=freeze_lineup(profiles,out,'0'*64)
            self.assertEqual(len(list(out.iterdir())),1)
            source.write_bytes(b'new')
            self.assertEqual((out/frozen['profiles']['a']['frozen_binary']).read_bytes(),b'old')
            (out/frozen['profiles']['a']['frozen_binary']).write_bytes(b'tampered')
            with self.assertRaises(ValueError):freeze_lineup(profiles,out,'0'*64)

    def test_running_parameters_must_really_be_declared(self):
        environment={};assigned={'B':'probe'};profiles={'probe':{'params':['flag:=true']}}
        data={'/rmtc_selfplay_B':{'ros__parameters':{'team':'B','stage':2,'flag':True}}}
        with patch('strategy_profiles.subprocess.check_output',return_value=json.dumps(data)) as call:
            audit_parameters(environment,assigned,profiles,{'flag'})
            self.assertIn('/rmtc_selfplay_B',call.call_args.args[0])
        data['/rmtc_selfplay_B']['ros__parameters'].pop('flag')
        with patch('strategy_profiles.subprocess.check_output',return_value=json.dumps(data)):
            with self.assertRaises(ValueError):audit_parameters(environment,assigned,profiles,{'flag'})

    def test_only_focal_version_may_differ_in_paired_runs(self):
        old={'run':{'lineup_schedule':'cyclic','evaluator_sha256':'same','evaluator_modules':{}},
             'row':{'seat':'A','arrangement':0,'controllers':{t:{'profile':t,'binary_sha256':'same','planner_params':[]} for t in 'ABCD'}}}
        new=copy.deepcopy(old);new['row']['controllers']['A']['binary_sha256']='new';validate_profile_pair(old,new)
        new['row']['controllers']['C']['planner_params']=['flag:=true']
        with self.assertRaises(ValueError):validate_profile_pair(old,new)

    def test_league_does_not_count_raw_excess_as_points(self):
        row = dict(seat='A', score=60, resource=130, resource_points=30,
                   combat_points=15, task_points=15, damage=900, rank=1,
                   first=True, missions=3, hp_zero_deaths=2, resource_at_60=50,
                   resource_at_100=130, actions={'move':10}, opening_reasons={'resource':4},
                   completed_ids=['collect_normal', 'collect_high_value', 'reach_target'])
        result = aggregate([row])
        self.assertEqual(result['mean_resource_points'], 30)
        self.assertEqual(result['mean_combat_points'], 15)
        self.assertEqual(result['all_three'], 1)
        self.assertEqual(result['completed_ids']['collect_high_value'], 1)

    def test_full_schedule_case_keys_do_not_collide(self):
        keys=[case_key({'variant':'m','seat':next(t for t,n in a.items() if n=='a'),'repetition':1,'arrangement':i})
              for i,a in arrangements(list('abcd'),'all')]
        self.assertEqual(len(set(keys)),24)
        self.assertEqual(case_key({'variant':'m','seat':'A'}),('m','A',1))


if __name__=='__main__':unittest.main()
