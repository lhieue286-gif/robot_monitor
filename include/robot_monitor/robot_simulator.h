#ifndef ROBOT_MONITOR_ROBOT_SIMULATOR_H
#define ROBOT_MONITOR_ROBOT_SIMULATOR_H

#include <ros/ros.h>
#include <geometry_msgs/Twist.h>

#include <robot_monitor/ResetRobot.h>
#include "robot_monitor/robot_model.h"

//机器人ros话题发布
namespace robot_monitor
{
class RobotSimulator
{
public:
    RobotSimulator();
    ~RobotSimulator();

private:
    void timerCallback(const ros::TimerEvent& event); // 定时器回调函数
    /**
     * 当订阅的 /cmd_vel 话题收到一条 geometry_msgs::Twist 消息时，ROS 自动调用这个回调函数，并把收到的消息通过 msg 传进来。
     * 接收外部发布的msg数据
     */
    void cmdVelCallback(const geometry_msgs::Twist::ConstPtr& msg); 
    void publishState(); //向外发布机器人的状态
    /**
     * robot_monitor:包名
     * ResetRobot自己创建的服务类型
     * Request:客户端发送给服务端的数据。
     * Response:服务端处理完成后返回给客户端的数据。
     */
    bool resetCallback(robot_monitor::ResetRobot::Request& req,robot_monitor::ResetRobot::Response& res);

    ros::NodeHandle nh_; // 封装了ROS中的一些常用的功能
    ros::NodeHandle pnh_; // ROS私有节点句柄，用于获取参数
    ros::Publisher state_pub_;  // 发布机器人状态的Publisher
    ros::Subscriber cmd_vel_sub_; // 订阅机器人速度指令的Subscriber
    ros::Timer timer_; // 定时器，用于定时更新机器人状态
    ros::ServiceServer reset_service_; // 服务服务器，用于重置机器人状态

    RobotModel model_; //创建机器人运动的类

    double publish_rate_; // 定时器的频率
    double low_battery_threshold_; //最低的电量

};

} // namespace robot_monitor

#endif