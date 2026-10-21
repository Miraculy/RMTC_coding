"""Synthetic public-protocol test, isolated from the practice judge (ROS domain 44)."""
import json, os, pathlib, signal, subprocess, time
os.environ.update(ROS_DOMAIN_ID='44',ROS_LOCALHOST_ONLY='0',RMW_IMPLEMENTATION='rmw_fastrtps_cpp')
import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
from std_msgs.msg import String
root=pathlib.Path('/root/ros2_ws');out=root/'test-results'/'wire';out.mkdir(parents=True,exist_ok=True)
stream=(out/'process.log').open('w')
bot=subprocess.Popen([str(root/'install/rmtc_bot/lib/rmtc_bot/contestant'),'--ros-args','-p','stage:=2','-p','ai_enabled:=false','-p',f'log_dir:={out}'],stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
rclpy.init();node=rclpy.create_node('contestant_wire_test')
q=QoSProfile(depth=1,reliability=ReliabilityPolicy.RELIABLE,durability=DurabilityPolicy.VOLATILE)
pub=node.create_publisher(String,'/team_A/perception',q);actions=[]
sub=node.create_subscription(String,'/team_A/action',lambda msg:actions.append(json.loads(msg.data)),q)
def spin(seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:rclpy.spin_once(node,timeout_sec=.02)
def frame(session,tick):
    return {'session_id':session,'tick':tick,'vision_radius':4,'map':{'width':24,'height':24},
            'self':{'team':'A','position':[8,8],'hp':100,'alive':True,'protected':False,'last_action':None},
            'visible_entities':[],'score':{'resource_value':0,'effective_damage':0,'mission_completed_ids':[]},'missions':[],'event_text':''}
def send(value):
    m=String();m.data=json.dumps(value);pub.publish(m)
def expect(session,tick):
    before=len(actions);send(frame(session,tick));spin(.25)
    assert len(actions)>before,(session,tick,'no action')
    assert actions[-1]['session_id']==session and actions[-1]['tick']==tick,actions[-1]
try:
    end=time.monotonic()+5
    while pub.get_subscription_count()==0 and time.monotonic()<end:spin(.05)
    assert pub.get_subscription_count()==1,'contestant did not subscribe'
    expect('wire-1',0);first=actions[-1];expect('wire-1',0);assert actions[-1]==first,'duplicate Tick changed cached action'
    expect('wire-1',1)
    before=len(actions);send(frame('wire-1',0));spin(.2);assert len(actions)==before,'old Tick produced an action'
    for corrupt in ['bad-json',{'tick':True},dict(frame('wire-1',2),tick=True)]:
        if isinstance(corrupt,str):
            m=String();m.data=corrupt;pub.publish(m)
        else:send(corrupt)
        spin(.15)
    assert len(actions)==before,'corrupt perception produced a guessed action'
    expect('wire-2',0)
    before=len(actions);send(frame('wire-1',2));spin(.2);assert len(actions)==before,'retired session was reactivated'
    expect('wire-2',1)
    assert bot.poll() is None,'contestant crashed'
    summary={'passed':True,'checks':['DDS action roundtrip','duplicate Tick replay','old Tick discard','malformed input recovery','session reset','retired session discard'],'actions':len(actions)}
    (out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary))
finally:
    node.destroy_node();rclpy.shutdown()
    if bot.poll() is None:
        os.killpg(bot.pid,signal.SIGINT)
        try:bot.wait(timeout=15)
        except subprocess.TimeoutExpired:os.killpg(bot.pid,signal.SIGKILL);bot.wait()
    stream.close()
