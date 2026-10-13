#include <ros/ros.h>
#include <iostream>
#include "robot_monitor/robot_simulator.h"

int main(int argc, char** argv)
{
    ros::init(argc, argv, "robot_simulator"); // 初始化ROS节点
    robot_monitor::RobotSimulator simulator; // 创建机器人模拟器对象
    std::cout << "模拟机器人启动"<<std::endl;
    ros::spin(); // 进入ROS事件循环
    return 0;
}