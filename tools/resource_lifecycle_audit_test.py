import unittest
from resource_lifecycle_audit import analyze


def frame(tick,position=(0,0),resources=None):
    return dict(kind='decision',session_id='one',tick=tick,position=list(position),hp=100,
        alive=True,vision_radius=4,map_size=[9,9],known_obstacles=[],resource_value=0,
        observed_resources=resources or [],observed_enemies=[],action={'action':'move'},
        target=None,reason='explore',goal_reason='explore',completed=[],
        public_missions=[{'type':'high_resources','id':'high'}])


class LifecycleTest(unittest.TestCase):
    def test_own_delta_and_position_confirm_collection(self):
        resource={'id':'r','position':[1,0],'value':10}
        a=frame(0,resources=[resource]);a.update(target=[1,0],reason='resource',goal_reason='resource')
        b=frame(1,(1,0));b['resource_value']=10
        result=analyze([a,b],2);self.assertEqual(result['resources'][0]['outcome'],'own_collection_confirmed')
        b['resource_value']=0
        self.assertEqual(analyze([a,b],2)['resources'][0]['outcome'],'visible_disappearance')

    def test_revival_on_resource_can_confirm_collection(self):
        resource={'id':'r','position':[1,0],'value':10}
        a=frame(0,resources=[resource]);b=frame(1);b.update(alive=False,hp=0)
        c=frame(2,(1,0));c['resource_value']=10
        self.assertEqual(analyze([a,b,c],3)['resources'][0]['outcome'],'own_collection_confirmed')

    def test_dead_frame_does_not_erase_unseen_resources(self):
        a=frame(0,resources=[{'id':'r','position':[1,0],'value':20}]);b=frame(1);b.update(alive=False,hp=0)
        self.assertEqual(analyze([a,b],2)['resources'][0]['outcome'],'unresolved')

    def test_offscreen_absence_does_not_prove_disappearance(self):
        a=frame(0,resources=[{'id':'r','position':[4,4],'value':10}]);b=frame(1,(0,0));b['vision_radius']=3
        result=analyze([a,b],2)
        self.assertEqual(result['resources'][0]['outcome'],'unresolved')

    def test_switch_and_nonmoving_action_are_logged_without_proven_loss(self):
        resource={'id':'r','position':[1,0],'value':10}
        a=frame(0,resources=[resource]);a.update(target=[1,0],goal_reason='resource',reason='combat',action={'action':'defend'})
        b=frame(1,resources=[resource]);b['target']=[0,5]
        item=analyze([a,b],2)['resources'][0]
        self.assertEqual([x['kind'] for x in item['interruptions']],['selected_but_not_moving','target_changed_while_unresolved'])
        self.assertEqual(item['outcome'],'unresolved')
        self.assertTrue(item['passed_over_for_exploration'])

    def test_information_gap_does_not_invent_unknown_resources(self):
        data=analyze([frame(0)],1)
        self.assertTrue(data['unseen_high_task_information_gap']);self.assertEqual(data['resources'],[])

    def test_rejects_missing_mixed_and_event_histories(self):
        a=frame(0);b=frame(1);b['session_id']='other'
        for records in [[a],[a,b],[a,dict(kind='ai',accepted=True,event_memory={'events':[{'kind':'illusion'}]})]]:
            with self.assertRaises(ValueError):analyze(records,2 if len(records)==1 or records[-1].get('kind')=='decision' else 1)


if __name__=='__main__':unittest.main()
