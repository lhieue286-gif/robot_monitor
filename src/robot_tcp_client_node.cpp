#include <ros/ros.h>

#include "robot_monitor/robot_tcp_client.h"

int main(int argc, char** argv)
{
    ros::init(argc,argv,"robot_tcp_client");

    robot_monitor::RobotTcpClient client;

    if (!client.start())
    {
        ROS_ERROR("RobotTcpClient start failed");

        return -1;
    }

    ros::spin();

    return 0;
}