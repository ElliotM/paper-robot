#!/usr/bin/env python
import roslib; roslib.load_manifest('manipulate_paper')
import rospy
import ee_cart_imped_action
import ee_cart_imped_control.control_switcher
import arm_navigation_msgs.msg
import arm_navigation_msgs.srv
import actionlib
import geometry_msgs.msg
from std_msgs.msg import String
from pr2_pick_and_place_demos.pick_and_place_manager import *
from object_manipulator.convert_functions import *

table_offset_x=.055
table_offset_z=.1

left_arm = None
right_arm = None

#NOTE: You must run the following stacks to get this to work
# roslaunch ee_cart_imped_launch load_ee_cart_imped.launch
# roslaunch pr2_3dnav both_arms_navigation.launch

class SimplePickAndPlaceExample():

    def __init__(self):
        rospy.loginfo("initializing pick and place manager")
        self.papm = PickAndPlaceManager()
        rospy.loginfo("finished initializing pick and place manager")

    def pick_up_object_near_point(self,target_point, whicharm):
        self.papm.call_tabletop_detection(update_table=1,update_place_rectangle =1,
                                          clear_attached_objects =1)

        success = self.papm.pick_up_object_near_point(target_point,whicharm)
        if success:
            rospy.loginfo("pick up was successful")
        else:
            rospy.loginfo("pick up failed")

    def place_object(self, whicharm, place_rect_dims, place_rect_center):
        rospy.info("Putting down the object in the %s gripper"\
                       % self.papm.arm_dict[whicharm])
        success = self.papm.put_down_object(whicharm,
                                            max_place_tries=25,
                                            use_place_override=1)
        if success:
            rospy.loginfo("place returned success")
        else:
            rospy.loginfo("place returned failure")
        return success

def move_object(table_edge_x, table_z, obj_offset=.02):
    sppe = SimplePickAndPlaceExample()
    target_point_xyz = [float(table_edge_x)+.2,0,float(table_z)]
    target_point = create_point_stamped(target_point_xyz,'/torso_lift_link')
    success = sppe.pick_up_object_near_point(target_point,0)

    if success:
        place_rect_dims = [.3, .3]
        center_xyz = [float(table_edge_x)+obj_offset,0,float(table_z)]
        center_quat = [0,0,01]
        place_rect_center = create_point_stamped(center_xyz+center_quat,'/torso_lift_link')
        rospy.loginfo("Started placement!")
        sppe.place_object(0,place_rect_dims,place_rect_center)
        rospy.loginfo("Finished placement!")


class ArmControl:
    def __init__(self, arm_name):
        self.arm_name = arm_name
        self.switchToForceControl()
        self.force_control = ee_cart_imped_action.EECartImpedClient\
            (self.arm_name)
        #self.switchToArmNavigationControl()
        #self.move_arm_control =\
        #    actionlib.SimpleActionClient\
        #    ('/move_'+ self.arm_name,\
        #         arm_navigation_msgs.msg.MoveArmAction)
        #self.move_arm_control.wait_for_server()
        
    def switchToArmNavigationControl_old(self):
        rospy.loginfo('Switching to arm navigation control on arm %s', self.arm_name)
        #ee_cart_imped_control.control_switcher.PR2CMClient.load_cartesian\
        #    (self.arm_name)
    def switchToForceControl(self):
        rospy.loginfo('Switching to force control on arm %s', self.arm_name)
        ee_cart_imped_control.control_switcher.PR2CMClient.load_ee_cart_imped\
            (self.arm_name)
    def moveToPoseForceControl(self,pose_stamped,time=6):
        self.switchToForceControl()
        self.force_control.moveToPoseStamped(pose_stamped, time)
    def moveToPoseArmNavControl_old(self,pcon,ocon,ocon_forearm):
        self.switchToArmNavigationControl()
        goal = arm_navigation_msgs.msg.MoveArmGoal()
        goal.planner_service_name = '/ompl_planning/plan_kinematic_path'
        goal.motion_plan_request.goal_constraints.position_constraints.append(pcon)
#        goal.motion_plan_request.goal_constraints.position_constraints.append(pcon_f)
        goal.motion_plan_request.goal_constraints.orientation_constraints.append(ocon)
        goal.motion_plan_request.path_constraints.orientation_constraints.append(ocon_forearm)
        #state_srv = rospy.ServiceProxy('/environment_server/get_robot_state',
        #                               arm_navigation_msgs.srv.GetRobotState)
        #state_resp = state_srv()
        #goal.motion_plan_request.start_state = state_resp.robot_state
        goal.motion_plan_request.planner_id = "SBLkConfig1"
        goal.motion_plan_request.group_name = self.arm_name
        goal.motion_plan_request.num_planning_attempts =2
        goal.motion_plan_request.allowed_planning_time  = rospy.Duration(30.0)
        self.move_arm_control.send_goal_and_wait(goal,rospy.Duration(60.0))
        result = self.move_arm_control.get_result()
        state = self.move_arm_control.get_state()
        return (result, state)
        

    
def reset_arms():
    right_pose = formStampedPose(0.1, -0.75, 0, 0, 0, 0,)
    left_pose = formStampedPose(0.1, 0.75, 0, 0, 0, 0,)
    right_arm.moveToPoseForceControl(right_pose, 5)
    left_arm.moveToPoseForceControl(left_pose,5)

def move_arm(arm_name,x,y,z,xOR,yOR,zOR,ref_frame):
    control = ee_cart_imped_action.EECartImpedClient(arm_name)
    print x,y,z,xOR,yOR,zOR
    control.addTrajectoryPoint(float(x),float(y),float(z),float(xOR),float(yOR),float(zOR),1,
                               700,700,700,30,30,30,
                               False, False, False, False, False,
                               False, 4, ref_frame)
    control.sendGoal()

def move_to_table_edge(table_x, table_z, arm_name="right_arm"):
    arm = ArmControl(arm_name)

    pose_stamped = formStampedPose(float(table_x)-table_offset_x,0.0,float(table_z)-table_offset_z,0.1,0.0,1.0)
    rospy.loginfo("Table x and y: %s %s" % (table_x,table_z))

    arm.moveToPoseForceControl(pose_stamped, 5)

def test_arm_position(table_x):
    arm = right_arm
    pose_stamped_h = formStampedPose(float(table_x)-table_offset_x,0.0,-.2,0.1,0.0,1.0)
    pose_stamped_f = formStampedPose(float(table_x)-table_offset_x,0.0,2.3,0.1,0.0,1.0)
    #arm.moveToPoseForceControl(pose_stamped,5)
    pcon_h = formPositionConstraint(pose_stamped_h)
    pcon_f = formPositionConstraint(pose_stamped_f,"r_elbow_flex_link",5.0) 
    ocon = formOrientationCon(pose_stamped_h,"r_wrist_roll_link")
    ocon2 = formOrientationCon(pose_stamped_f)
    arm.moveToPoseArmNavControl(pcon_h,ocon,ocon2)
    
def formPositionConstraint(pose_stamped, target_frame="r_wrist_roll_link",region_dim=.02):
    pcon = arm_navigation_msgs.msg.PositionConstraint()
    pcon.header = pose_stamped.header
    pcon.position = pose_stamped.pose.position
    pcon.link_name=target_frame
    pcon.constraint_region_shape.type=\
        pcon.constraint_region_shape.BOX
    for i in range(3):
        pcon.constraint_region_shape.dimensions.append(region_dim)
    pcon.constraint_region_orientation.w=1.0
    pcon.weight=1.0
    return pcon

def formOrientationCon(pose_stamped, link_name="r_forearm_link"):
    ocon = arm_navigation_msgs.msg.OrientationConstraint()
    ocon.header = pose_stamped.header
    ocon.link_name = link_name
    ocon.type = ocon.HEADER_FRAME
    ocon.orientation.x =0.0
    ocon.orientation.y =1.0
    ocon.orientation.z =0.0
    ocon.orientation.w =1.0
    ocon.absolute_roll_tolerance = 0.1
    ocon.absolute_pitch_tolerance =0.1
    ocon.absolute_yaw_tolerance = 0.1
    ocon.weight=1.0
    return ocon
    
def formStampedPose(x,y,z,xOR,yOR,zOR,wOR=1.0,target_frame="/torso_lift_link"):
    pose_stamped = geometry_msgs.msg.PoseStamped()
    pose_stamped.header.frame_id=target_frame
    pose_stamped.pose.position.x = x
    pose_stamped.pose.position.y = y
    pose_stamped.pose.position.z = z
    pose_stamped.pose.orientation.x = xOR
    pose_stamped.pose.orientation.y = yOR
    pose_stamped.pose.orientation.z = zOR
    pose_stamped.pose.orientation.w = wOR
    return pose_stamped
    
def perform_basic_fold(table_x, table_z, arm_name="right_arm"):
    reset_arms()
    fold_dist_x = .06
    fold_dist_z = .00
    rospy.loginfo("Table x and y: %s %s" % (table_x,table_z))
    pos1 = formStampedPose(float(table_x)-table_offset_x,0.0,float(table_z)-table_offset_z,
                           0.1,0.0,1.0)
    pos2 = formStampedPose(float(table_x)-table_offset_x,0.0,float(table_z)+2*table_offset_z,
                           0.1,0.0,1.0)
    pos3 = formStampedPose(float(table_x)-table_offset_x,0.0,float(table_z)+2*table_offset_z,
                           -1.0,0.0,0.0,wOR=0.0)
    pos4 = formStampedPose(float(table_x)+fold_dist_x,0.0,float(table_z)+table_offset_z + fold_dist_z,
                           -1.0,0.0,0.0, wOR=0.0)
    right_arm.moveToPoseForceControl(pos1)
    right_arm.moveToPoseForceControl(pos2)
    right_arm.moveToPoseForceControl(pos3)
    right_arm.moveToPoseForceControl(pos4)

def final_pos(table_x,table_z):
    fold_dist_x=.06
    fold_dist_z=0
    pos1 = formStampedPose(float(table_x)+fold_dist_x,0.0,float(table_z)+table_offset_z + fold_dist_z,
                           -1.0,0.0,0.0,wOR=0.0)
    right_arm.moveToPoseForceControl(pos1)
    
def main():
    rospy.init_node('force_control_test', anonymous=True)
    global left_arm
    global right_arm
    left_arm = ArmControl("left_arm")
    right_arm = ArmControl("right_arm")
    rospy.loginfo(rospy.get_name() + "About to start subscription")
    rospy.Subscriber('force_control_commands',String, callback)
    rospy.spin()
    
def callback(data):
    rospy.loginfo(rospy.get_name() + ": I heard %s" % data.data)
    args = data.data.split(" ")
    print locals()
    func_to_cal = globals()[args[0]]
    apply(func_to_cal,args[1:])
    rospy.loginfo("Finished callback")

def tilt_arm(val):
    client = actionlib.SimpleActionClient('move_right_arm',MoveArmAction)
    client.wait_for_server()

    goal = MoveArmGoal()
    goal.header.frame_id="/torso_lift_link"

    action = ArmAction()
    action.type= ArmAction.MOVE_ARM
    action.goal.orientation=1.0

if __name__=='__main__':
    main()    

    
