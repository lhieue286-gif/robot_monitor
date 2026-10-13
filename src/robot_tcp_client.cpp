#include "robot_monitor/robot_tcp_client.h"

#include <arpa/inet.h>
#include <cerrno>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

#include "robot_monitor/protocol.h"

namespace robot_monitor
{
    RobotTcpClient::RobotTcpClient()
        : nh_(),
          pnh_("~"),
          server_ip_("127.0.0.1"),
          server_port_(8080),
          robot_id_(-1),
          socket_fd_(-1),
          sequence_(1),
          state_stream_enabled_(false)
    {
        /**
         * state_stream_enabled_(false) : 表示机器人刚启动时不通过TCP发送状态。
         */
        state_sub_ = nh_.subscribe("robot_state",10,&RobotTcpClient::robotStateCallback,this);
        cmd_vel_pub_ = nh_.advertise<geometry_msgs::Twist>("cmd_vel",10);// 发布控制指令
        // 可以通过rosrun robot_monitor robot_tcp_client_node _robot_id:=1进行改进
        pnh_.param("server_ip",server_ip_,std::string("127.0.0.1"));
        pnh_.param("server_port",server_port_,8080);
        pnh_.param("robot_id",robot_id_,-1);
    }
    RobotTcpClient::~RobotTcpClient()
    {
        if (socket_fd_ != -1)
        {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        
    }
    bool RobotTcpClient::connectToServer()
    {
        // 1. 创建 TCP socket
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);

        if (socket_fd_ == -1)
        {
            perror("socket");
            return false;
        }

        // 2. 设置中心服务器地址
        struct sockaddr_in server_addr{};

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(static_cast<uint16_t>(server_port_));
        // 字符串 IP -> 网络地址
        if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr.s_addr) != 1)
        {
            ROS_ERROR(
                "invalid server ip: %s",
                server_ip_.c_str());

            close(socket_fd_);
            socket_fd_ = -1;

            return false;
        }
        // 3.主动连接中心服务器
        int ret = connect(socket_fd_,reinterpret_cast<struct sockaddr*>(&server_addr),sizeof(server_addr));
        if (ret == -1)
        {
            perror("connect");

            close(socket_fd_);
            socket_fd_ = -1;

            return false;
        }

        std::cout << "================"<<std::endl;
        std::cout << "机器人客户端启动" << std::endl;
        std::cout << "机器人"
                  << robot_id_
                  << ",连接到中心服务器,ip:"
                  << server_ip_.c_str()
                  << "端口号 " << server_port_
                  << std::endl;

        return true;
    }
    bool RobotTcpClient::sendAll(const std::vector<uint8_t>& data)
    {
        std::size_t total_send = 0;
        while (total_send < data.size())
        {
            ssize_t n = send(
                socket_fd_,
                data.data() + total_send,
                data.size() - total_send,
                MSG_NOSIGNAL);

            if (n > 0)
            {
                total_send += static_cast<std::size_t>(n);
            }
            if (n == -1 && errno == EINTR)
            {
                continue;
            }

            if (n == -1)
            {
                perror("send");
                return false;
            }

            if (n == 0)
            {
                return false;
            }
        }
        return true;
    }
    bool RobotTcpClient::sendRegister()
    {
        // robot_id 必须有效
        if (robot_id_ < 0)
        {
            std::cerr<< "robot_id is invalid: " << robot_id_ << std::endl;
            return false;
        }
        RegisterData register_data;

        // 当前 TCP 客户端身份是机器人
        register_data.role = ROLE_ROBOT;

        // 告诉中心服务器我是哪一台机器人
        register_data.robot_id = robot_id_;

        // RegisterData --> TCP Packet

        std::vector<uint8_t> packet = serializeRegisterPacket(sequence_++, register_data);

        if (!sendAll(packet))
        {
            std::cerr << "Robot " << robot_id_ << " 发送注册消息失败" << std::endl;
            return false;
        }

        std::cout << "机器人" << robot_id_ << "注册信息发布成功"<< std::endl;
        return true; 
    }

    bool RobotTcpClient::connectAndRegister()
    {

        if (socket_fd_ != -1)
        {
            std::cerr << "Robot " << robot_id_ << " 已经存在TCP连接,不重复连接" << std::endl;
            return true;
        }

        // 丢弃旧连接残留的半包数据
        recv_buffer_.clear();

        if (!connectToServer())
        {
            return false;
        }

        if (!sendRegister())
        {
            if (socket_fd_ != -1)
            {
                close(socket_fd_);
                socket_fd_ = -1;
            }

            return false;
        }

        return true;
    }

    void RobotTcpClient::handleDisconnect(const std::string &reason)
    {
        std::cerr << "Robot "
                  << robot_id_
                  << " TCP连接断开"
                  << ", reason = "
                  << reason
                  << std::endl;

        if (socket_fd_ != -1)
        {
            close(socket_fd_);
            socket_fd_ = -1;
        }

        // 旧连接的半包不能保留到新连接
        recv_buffer_.clear();

        // 断线后停止上传状态
        state_stream_enabled_ = false;

        // 停止当前连接相关定时器
        receive_timer_.stop();
        heartbeat_timer_.stop();

        // 开始每2秒尝试重连
        reconnect_timer_.start();
    }

    void RobotTcpClient::reconnectTimerCallback(const ros::TimerEvent &event)
    {
        // 已经连接成功，不需要重连
        if (socket_fd_ != -1)
        {
            reconnect_timer_.stop();
            return;
        }

        std::cout << "Robot "
                  << robot_id_
                  << " 正在尝试重新连接Gateway..."
                  << std::endl;

        if (!connectAndRegister())
        {
            std::cerr << "Robot "
                      << robot_id_
                      << " 重连失败，2秒后继续尝试"
                      << std::endl;
            std::cout << "================"<<std::endl;

            // reconnect_timer_是周期定时器，
            // 不需要再次调用start()
            return;
        }

        std::cout << "Robot "
                  << robot_id_
                  << " 已重新连接并注册成功"
                  << std::endl;

        // 重连成功
        reconnect_timer_.stop();

        // 恢复接收Gateway消息
        receive_timer_.start();

        // 恢复心跳
        heartbeat_timer_.start();
    }

    // RobotTcpClient接收Gateway消息
    void RobotTcpClient::receiveTimerCallback(const ros::TimerEvent &event)
    {
        if (socket_fd_ == -1)
        {
            return ;
        }

        while (true)
        {
            uint8_t temp_buffer[4096];
            // MSG_DONTWAIT：本次recv使用非阻塞方式
            ssize_t len = recv(socket_fd_,temp_buffer,sizeof(temp_buffer), MSG_DONTWAIT);
            if (len > 0)
            {
                recv_buffer_.insert(recv_buffer_.end(),temp_buffer,temp_buffer + len);
                if (! parseReceivedPackets())
                {
                     handleDisconnect("Gateway消息解析失败");
                     return;
                }
                continue; // 继续recv，直到读空socket  
            }
            if (len == 0)
            {
                handleDisconnect("Gateway关闭了TCP连接");
                return;
            }
            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 当前socket已经没有更多数据
                return;
            }

            const std::string reason = std::string("recv失败: ") + std::strerror(errno);
            handleDisconnect(reason);

            return;
        }  
    }
    // 解析状态发送请求
    bool RobotTcpClient::parseReceivedPackets(){
        while (true)
        {
            Packet packet;
            std::size_t consumed = 0;

            const ParseResult result = tryParsePacket(recv_buffer_, packet,consumed);
            if (result == PARSE_NEED_MORE)
            {
                // 半包，等待下一次recv
                return true;
            }

            if (result == PARSE_ERROR)
            {
                std::cerr << "收到非法Packet" << std::endl;
                return false;
            }

            if (packet.header.type == MSG_STATE_STREAM_REQUEST) //机器人客户端解析监控客户端发送过来的状态请求指令
            {
                StateStreamRequestData request{};
                if (!payloadToStateStreamRequest(packet, request))
                {
                    std::cerr << "状态发送请求解析失败" << std::endl;
                    return false;
                }
                // 再次检查是不是目标机器人
                if (request.robot_id != robot_id_)
                {
                    std::cerr << "状态请求robot_id不匹配"
                              << ", 当前机器人 = "
                              << robot_id_
                              << ", 请求机器人 = "
                              << request.robot_id
                              << std::endl;
                }
                else if (request.duration_ms == 0)
                {
                    // 0表示立即停止
                    state_stream_enabled_ = false;
                    std::cout << "Robot " << robot_id_ << " 停止发送状态" << std::endl;
                }
                else if (request.duration_ms > 30000)
                {
                    std::cerr << "状态发送时间不能超过30秒" << std::endl;
                }
                else
                {
                    state_stream_enabled_ = true;
                    state_stream_deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(request.duration_ms);
                    std::cout << "Robot " << robot_id_ << " 开始发送状态" << ", duration_ms = " << request.duration_ms << std::endl;
                }
            }

            if (packet.header.type == MSG_SET_VELOCITY) //机器人客户端解析控制指令
            {
                SetVelocityData command;

                if (!payloadToSetVelocity(packet,command))
                {
                    std::cerr << "速度控制命令解析失败" << std::endl;

                    return false;
                }
                if (command.robot_id != robot_id_)
                {
                    std::cerr << "速度命令robot_id不匹配"
                              << ", 当前机器人 = " << robot_id_
                              << ", 命令机器人 = " << command.robot_id
                              << std::endl;
                }
                else
                {
                    CommandResultData command_result{};
                    command_result.command_sequence = packet.header.sequence;
                    command_result.robot_id = robot_id_;
                    if (cmd_vel_pub_.getNumSubscribers() == 0)
                    {
                        std::cerr << "cmd_vel当前没有订阅者"
                                  << ", robot_id = "
                                  << robot_id_
                                  << std::endl;

                        command_result.result = COMMAND_EXECUTE_FAILED;
                    }
                    else
                    {
                        geometry_msgs::Twist cmd_vel{};
                        cmd_vel.linear.x = command.linear_velocity;
                        cmd_vel.angular.z = command.angular_velocity;
                        cmd_vel_pub_.publish(cmd_vel);
                        command_result.result = COMMAND_SUCCESS;
                        std::cout << "已执行速度控制命令"
                                  << ", robot_id = " << robot_id_
                                  << ", linear = " << command.linear_velocity
                                  << ", angular = " << command.angular_velocity
                                  << std::endl;
                    }
                    // 把执行结果发回Gateway
                    std::vector<uint8_t> result_packet =
                        serializeCommandResultPacket(
                            sequence_++,
                            command_result);
                    if (!sendAll(result_packet))
                    {
                        std::cerr << "发送控制执行结果失败" << std::endl;
                        return false;
                    }
                }
            }
            
            // 删除已经解析完成的Packet
            recv_buffer_.erase(
                recv_buffer_.begin(),
                recv_buffer_.begin() + consumed);
        }
        
    }
    void RobotTcpClient::robotStateCallback(const robot_monitor::RobotState::ConstPtr& msg)
    {
        if (socket_fd_ == -1)
        {
            return;
        }
        // Monitor还没有请求状态
        if (!state_stream_enabled_)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now(); // 时间理解一下

        // 已经超过发送截止时间
        if (now >= state_stream_deadline_)
        {
            state_stream_enabled_ = false;

            std::cout
                << "Robot "
                << robot_id_
                << " 状态发送时间结束"
                << std::endl;

            return;
        }

        RobotStateData state{};
        // state.robot_id = msg->robot_id;
        state.robot_id = robot_id_;
        state.timestamp_sec = msg->timestamp.sec;
        state.timestamp_nsec = msg->timestamp.nsec;

        state.x = msg->x;
        state.y = msg->y;
        state.yaw = msg->yaw;

        state.linear_velocity = msg->linear_velocity;
        state.angular_velocity = msg->angular_velocity;

        state.battery = msg->battery;
        std::vector<uint8_t> packet =
            serializeRobotStatePacket(
                sequence_++,
                state);
        if (!sendAll(packet))
        {
            handleDisconnect("发送RobotState失败");
            return;
        }

        std::cout << "=================" << std::endl;
        std::cout << "robot_id = " << robot_id_ << std::endl;
        std::cout << "x = " << state.x << std::endl;
        std::cout << "y = " << state.y << std::endl;
        std::cout << "yaw= " << state.yaw << std::endl;
        std::cout << "linear_velocity = " << state.linear_velocity << std::endl;
        std::cout << "angular_velocity = " << state.angular_velocity << std::endl;
        std::cout << "battery = " << state.battery << std::endl;
    }
    void RobotTcpClient::heartbeatTimerCallback(const ros::TimerEvent& event)
    {
        if (socket_fd_ == -1)
        {
            return;
        }

        HeartbeatData heartbeat{};
        heartbeat.robot_id = robot_id_;

        std::vector<uint8_t> packet = serializeHeartbeatPacket(sequence_++, heartbeat);

        if (!sendAll(packet))
        {
            handleDisconnect("发送心跳失败");
            return;
        }
        
    }

    bool RobotTcpClient::start()
    {
        // 检查参数
        if (robot_id_ < 0)
        {
            ROS_ERROR( "robot_id must be >= 0");

            return false;
        }
        if (server_port_ <= 0 || server_port_ > 65535)
        {
            ROS_ERROR( "invalid server_port: %d",server_port_);

            return false;
        }
        // if (!connectToServer())
        // {
        //     return false;
        // }

        // // 第二步：发送身份注册
        // if (!sendRegister())
        // {
        //     return false;
        // }
        // 创建接收定时器，但暂时不启动 每10毫秒检查一次Gateway有没有发来消息
        receive_timer_ = nh_.createTimer(ros::Duration(0.01), &RobotTcpClient::receiveTimerCallback, this, false, false);
        // 每1秒发送一次心跳 创建心跳定时器，但暂时不启动
        heartbeat_timer_ = nh_.createTimer(ros::Duration(1.0),&RobotTcpClient::heartbeatTimerCallback,this,false,false);
        // 创建重连定时器，但暂时不启动
        reconnect_timer_ = nh_.createTimer(ros::Duration(2.0), &RobotTcpClient::reconnectTimerCallback, this, false, false);

        // 第一次连接
        if (connectAndRegister())
        {
            receive_timer_.start();
            heartbeat_timer_.start();
            std::cout << "Robot " << robot_id_ << " 首次连接并注册成功" << std::endl;
            return true;
        }
        // Gateway还没有启动时，Robot节点不能直接退出
        std::cout << "================"<<std::endl;
        std::cerr << "Robot " << robot_id_ << " 首次连接失败，进入自动重连" << std::endl;

        reconnect_timer_.start();

        return true;
    }

} // namespace robot_monitor
