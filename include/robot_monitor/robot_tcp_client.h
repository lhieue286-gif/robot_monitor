#ifndef ROBOT_MONITOR_ROBOT_TCP_CLIENT_H
#define ROBOT_MONITOR_ROBOT_TCP_CLIENT_H

#include <ros/ros.h>

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include "robot_monitor/RobotState.h"
#include <geometry_msgs/Twist.h>

namespace robot_monitor
{
class RobotTcpClient
{
public:
    RobotTcpClient();
    ~RobotTcpClient();
    // 启动 TCP 客户端：
    // 1.连接中心服务器
    // 2.发送机器人注册消息
    bool start();

private:
    bool connectToServer();                         // 连接中心服务器
    bool sendRegister();                            // 发送机器人身份注册消息
    bool sendAll(const std::vector<uint8_t> &data); // 保证一整个TCP Packet 全部发送完成
    void robotStateCallback(const robot_monitor::RobotState::ConstPtr &msg);
    void receiveTimerCallback(const ros::TimerEvent &event);   // 定时检查监控客户端是否发出状态请求
    bool parseReceivedPackets();                               // 解析TCP接收缓冲区中的Packet
    void heartbeatTimerCallback(const ros::TimerEvent &event); // 增加心跳定时器
    bool connectAndRegister();                                 // 连接Gateway并发送注册包
    void handleDisconnect(const std::string &reason);          // 统一处理TCP断线
    void reconnectTimerCallback(const ros::TimerEvent &event); // 自动重连定时器

private:
    // ROS 私有参数
    ros::NodeHandle pnh_;
    
    ros::NodeHandle nh_;
    ros::Subscriber state_sub_;
    ros::Publisher cmd_vel_pub_;

    // 中心服务器 IP
    std::string server_ip_;

    // 中心服务器端口
    int server_port_;

    //当前机器人ID
    int robot_id_;

    // 和中心服务器通信的 socket fd
    int socket_fd_;

    // TCP Packet 序列号
    uint32_t sequence_;

    // TCP接收缓冲区，处理粘包和半包
    std::vector<uint8_t> recv_buffer_;

    // 当前是否允许上传机器人状态
    bool state_stream_enabled_;

    // 状态上传截止时间
    std::chrono::steady_clock::time_point state_stream_deadline_;

    ros::Timer receive_timer_;   // 定时接收Gateway消息
    ros::Timer heartbeat_timer_; // 定时发送心跳
    ros::Timer reconnect_timer_; // 自动重连
};

} // namespace robot_monitor

#endif