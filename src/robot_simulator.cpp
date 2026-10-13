#include "robot_monitor/robot_simulator.h"
#include <robot_monitor/RobotState.h>

//机器人ros话题发布
namespace robot_monitor
{
    RobotSimulator::RobotSimulator()
        : pnh_("~"),
          publish_rate_(10.0),
          low_battery_threshold_(20.0)

    {
        // 1. 读取参数,局部定义下面几个参数,为了方便传递0
        int robot_id = 1;
        double max_linear_speed = 1.0;
        double max_angular_speed = 0.8;
        double initial_battery = 100.0;

        pnh_.param("robot_id", robot_id, 1);
        pnh_.param("max_linear_speed", max_linear_speed, 1.0);
        pnh_.param("max_angular_speed", max_angular_speed, 0.8);
        pnh_.param("publish_rate", publish_rate_, 10.0);
        if (publish_rate_ <= 0.0)
        {
            ROS_WARN("publish_rate must be > 0, use default 10Hz");
            publish_rate_ = 10.0;
        }
        pnh_.param("initial_battery", initial_battery, 100.0);
        pnh_.param("low_battery_threshold", low_battery_threshold_, 20.0);

        //进行机器人状态初始化
        model_.configure(robot_id,
                         max_linear_speed,
                         max_angular_speed,
                         initial_battery);
        // 2. 创建 ROS 通信对象
        state_pub_ = nh_.advertise<robot_monitor::RobotState>("robot_state", 10);
        cmd_vel_sub_ = nh_.subscribe("cmd_vel",10, &RobotSimulator::cmdVelCallback,this);
        reset_service_ = nh_.advertiseService("reset_robot", &RobotSimulator::resetCallback, this);

        timer_ = nh_.createTimer(ros::Duration(1.0 / publish_rate_), &RobotSimulator::timerCallback, this);
    }
    // 定时器回调函数，更新机器人状态并发布
    void RobotSimulator::timerCallback(const ros::TimerEvent& event)
    {
        double dt = (event.current_real - event.last_real).toSec();

        if (dt <= 0.0 || dt > 1.0)
        {
            dt = 0.1;
        }

        model_.update(dt);// 机器人根据dt更新位置、朝向和电量
        publishState(); //向外发布机器人的状态
    }

    // 速度指令回调函数，接收外部发布者发布的话题消息并处理速度指令消息
    void RobotSimulator::cmdVelCallback(const geometry_msgs::Twist::ConstPtr& msg)
    {
        model_.setVelocity(msg->linear.x,msg->angular.z);

        ROS_INFO("Received cmd_vel: linear_velocity=%.2f, angular_velocity=%.2f",model_.linearVelocity(),model_.angularVelocity());

    }

    // 服务回调函数，接收外部发布来的服务消息并重置机器人状态
    bool RobotSimulator::resetCallback(robot_monitor::ResetRobot::Request& req,robot_monitor::ResetRobot::Response& res)
    {
        model_.reset();
        res.success = true;
        res.message = "Robot state has been reset successfully.";
        ROS_INFO("Robot state has been reset successfully.");
        return true;
    }

    // 发布机器人的当前的的状态
    void RobotSimulator::publishState(){

        robot_monitor::RobotState state_msg;

        state_msg.robot_id = model_.robotId();
        state_msg.x = model_.x();
        state_msg.y = model_.y();
        state_msg.yaw = model_.yaw();
        state_msg.linear_velocity = model_.linearVelocity();
        state_msg.angular_velocity = model_.angularVelocity();
        state_msg.battery = model_.battery();
        state_msg.timestamp = ros::Time::now();

        state_pub_.publish(state_msg);
    }

    RobotSimulator::~RobotSimulator(){

    }

} // namespace robot_monitor
