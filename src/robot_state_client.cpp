#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>
#include <cstdint>
#include <cerrno>
#include <sys/select.h>
#include <sstream>
#include <string>

#include "robot_monitor/protocol.h"

// 监控客户端的代码
// 发送函数
bool sendAll(int fd , const std::vector<uint8_t>& data)
{
    std::size_t total_sent = 0;

    while (total_sent < data.size())
    {
        /**
         * MSG_NOSIGNAL ： 当你向一个已经断开的 TCP 连接发送数据时，不要让系统给当前进程发送 SIGPIPE 信号。
         * 对方断了也别把我整个程序杀掉，直接让 send() 返回错误就行。
         * n == -1
         * errno == EPIPE
         */
        ssize_t n = send(fd,data.data() + total_sent,data.size() - total_sent,MSG_NOSIGNAL);
        if (n > 0)
        {
            total_sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n == -1 && errno == EINTR)
        {
            continue;
        }

        if (n == -1)
        {
            perror("send register");
            return false;
        }
        if (n == 0)
        {
            return false;
        }   
    }
    return true; 
}
// 发送控制指令的函数
bool sendSetVelocity(
    int fd,
    uint32_t& sequence,
    int32_t robot_id,
    double linear_velocity,
    double angular_velocity)
{

    robot_monitor::SetVelocityData command;

    command.robot_id = robot_id;
    command.linear_velocity = linear_velocity;
    command.angular_velocity = angular_velocity;

    std::vector<uint8_t> packet =
        robot_monitor::serializeSetVelocityPacket(
            sequence++,
            command);

    if (!sendAll(fd, packet))
    {
        std::cerr
            << "发送速度控制命令失败"
            << std::endl;

        return false;
    }

    std::cout
        << "已发送速度控制命令"
        << ", robot_id = " << robot_id
        << ", linear = " << linear_velocity
        << ", angular = " << angular_velocity
        << std::endl;

    return true;
}
// 发送得到机器人消息的发送函数
bool sendStateStreamRequest(
    int fd,
    uint32_t& sequence,
    int32_t robot_id,
    uint32_t duration_ms)
{
    robot_monitor::StateStreamRequestData request{};

    request.robot_id = robot_id;

    request.duration_ms = duration_ms;

    std::vector<uint8_t> packet = robot_monitor::serializeStateStreamRequestPacket(sequence++,request);

    if (!sendAll(fd, packet))
    {
        std::cerr << "发送状态查看请求失败" << std::endl;

        return false;
    }

    if (duration_ms == 0)
    {
        std::cout << "已请求停止Robot " << robot_id << "发送状态" << std::endl;
    }
    else
    {
        std::cout  << "已请求Robot " << robot_id << "发送状态，持续 "  << duration_ms / 1000  << " 秒"  << std::endl;
    }

    return true;
}

int main(int argc, char const *argv[])
{
    int client_fd = socket(AF_INET,SOCK_STREAM,0);

    if (client_fd == -1)
    {
        perror("socket");
        return -1;
    }

    struct sockaddr_in server_addr{};

    server_addr.sin_family = AF_INET;

    server_addr.sin_port = htons(8080);

    inet_pton( AF_INET, "127.0.0.1", &server_addr.sin_addr );
    int ret = connect(client_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)); // 主动连接指定IP和端口的TCP服务器
    if(ret == -1){
        perror("connect error");
        close(client_fd);
        return -1;
    }
    std::cout << "已连接RobotGateway" << std::endl;

    // ================================
    // 连接成功后，首先向服务器注册身份
    // ================================

    robot_monitor::RegisterData register_data;
    // 当前这个程序是监控客户端
    register_data.role = robot_monitor::ROLE_MONITOR;
    register_data.robot_id = -1;

    uint32_t sequence = 1;
    // 将 RegisterData 序列化成 TCP Packet
    std::vector<uint8_t> register_packet = robot_monitor::serializeRegisterPacket(sequence ++,register_data);

    // 发送注册包
    if (!sendAll(client_fd,register_packet))
    {
        std::cerr << "发送客户端注册消息失败" << std::endl;
        close(client_fd);
        return -1;
    }
    std::cout << "已发送客户端注册消息" << ", role = MONITOR" << std::endl;

    std::vector<uint8_t> recv_buffer;

    std::cout
        << "支持以下命令："
        << std::endl;

    std::cout
        << "watch robot_id seconds"
        << "  例如:watch 2 5"
        << std::endl;

    std::cout
        << "watch robot_id 0"
        << "        例如:watch 2 0"
        << std::endl;

    std::cout
        << "vel robot_id linear angular"
        << "  例如:vel 2 0.5 0.2"
        << std::endl;

    while (true)
    {

        fd_set read_fds;

        FD_ZERO(&read_fds);

        // 监听服务器Socket
        FD_SET(client_fd, &read_fds);

        // 监听键盘输入
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd =
            std::max(client_fd, STDIN_FILENO);

        int ready = select(
            max_fd + 1,
            &read_fds,
            nullptr,
            nullptr,
            nullptr);

        if (ready == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("select");
            break;
        }

        // 后面分别处理键盘和TCP
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            std::string line;

            if (!std::getline(std::cin, line))
            {
                break;
            }

            std::istringstream iss(line);

            std::string command_type;

            if (!(iss >> command_type))
            {
                continue;
            }

            // =====================================
            // 查看机器人状态
            // watch robot_id seconds
            // =====================================

            if (command_type == "watch")
            {
                int32_t robot_id;
                int64_t duration_seconds;

                if (!(iss >> robot_id >>
                      duration_seconds))
                {
                    std::cout << "输入格式错误" << std::endl;

                    std::cout << "正确格式:watch robot_id seconds" << std::endl;

                    std::cout  << "例如:watch 2 5" << std::endl;

                    continue;
                }

                if (robot_id < 0)
                {
                    std::cout << "robot_id不能小于0" << std::endl;

                    continue;
                }

                if (duration_seconds < 0 || duration_seconds > 30)
                {
                    std::cout << "查看时间必须在0到30秒之间" << std::endl;

                    continue;
                }

                const uint32_t duration_ms = static_cast<uint32_t>( duration_seconds * 1000);

                if (!sendStateStreamRequest( client_fd, sequence, robot_id,  duration_ms))
                {
                    break;
                }
                // 客户端
            }

            // =====================================
            // 控制机器人速度
            // vel robot_id linear angular
            // =====================================

            else if (command_type == "vel")
            {
                int32_t robot_id;
                double linear_velocity;
                double angular_velocity;

                if (!(iss >> robot_id >> linear_velocity >> angular_velocity))
                {
                    std::cout
                        << "输入格式错误"
                        << std::endl;

                    std::cout
                        << "正确格式："
                        << "vel robot_id linear angular"
                        << std::endl;

                    std::cout
                        << "例如：vel 2 0.5 0.2"
                        << std::endl;

                    continue;
                }

                if (robot_id < 0)
                {
                    std::cout
                        << "robot_id不能小于0"
                        << std::endl;

                    continue;
                }

                if (!sendSetVelocity(
                        client_fd,
                        sequence,
                        robot_id,
                        linear_velocity,
                        angular_velocity))
                {
                    break;
                }
            }

            else
            {
                std::cout
                    << "未知命令："
                    << command_type
                    << std::endl;

                std::cout
                    << "可用命令：watch、vel"
                    << std::endl;
            }
        }
        if (FD_ISSET(client_fd, &read_fds))
        {
            uint8_t temp_buffer[4096];

            ssize_t len = recv(client_fd, temp_buffer, sizeof(temp_buffer), 0);

            if (len > 0)
            {
                recv_buffer.insert(recv_buffer.end(), temp_buffer, temp_buffer + len);

                while (true)
                {
                    robot_monitor::Packet packet;
                    std::size_t consumed = 0; // 解析数据的长度

                    robot_monitor::ParseResult result = robot_monitor::tryParsePacket(
                        recv_buffer, packet, consumed); // 已经将数据保存到packet

                    if (result == robot_monitor::PARSE_NEED_MORE)
                    {
                        // 连Header都没有完整
                        break;
                    }
                    if (result == robot_monitor::PARSE_ERROR)
                    {
                        std::cerr << "收到非法Packet" << std::endl;
                        close(client_fd);

                        return -1;
                    }
                    if (packet.header.type == robot_monitor::MSG_ROBOT_STATE)
                    {
                        robot_monitor::RobotStateData state;
                        if (!robot_monitor::payloadToRobotState(packet, state))
                        {
                            std::cerr << "机器人发送的状态结果解析失败" << std::endl;
                            close(client_fd);
                            return -1;
                        }

                        std::cout << "========== RobotState ==========" << std::endl;
                        std::cout << "sequence = " << packet.header.sequence << std::endl;
                        std::cout << "robot_id = " << state.robot_id << std::endl;
                        std::cout << "x = " << state.x << std::endl;
                        std::cout << "y = " << state.y << std::endl;
                        std::cout << "yaw = " << state.yaw << std::endl;
                        std::cout << "linear_velocity = " << state.linear_velocity << std::endl;
                        std::cout << "angular_velocity = " << state.angular_velocity << std::endl;
                        std::cout << "battery = " << state.battery << std::endl;
                        std::cout << "timestamp = " << state.timestamp_sec << "." << state.timestamp_nsec << std::endl;
                    }

                    if (packet.header.type == robot_monitor::MSG_ROBOT_STATUS)
                    {
                        robot_monitor::RobotStatusData status_data;

                        if (!robot_monitor::payloadToRobotStatus(packet, status_data))
                        {
                            std::cerr << "RobotStatus解析失败" << std::endl;
                        }
                        else
                        {
                            if (status_data.status == robot_monitor::ROBOT_OFFLINE)
                            {
                                std::cout << "\n==========================" << std::endl;
                                std::cout << "机器人离线" << std::endl;
                                std::cout << "robot_id = " << status_data.robot_id << std::endl;
                            }

                            if (status_data.status == robot_monitor::ROBOT_ONLINE)
                            {
                                std::cout
                                    << "\n机器人上线"
                                    << ", robot_id = "
                                    << status_data.robot_id
                                    << std::endl;
                            }
                        }
                    }

                    if (packet.header.type == robot_monitor::MSG_COMMAND_RESULT)
                    {
                        robot_monitor::CommandResultData result{};
                        if (!robot_monitor::payloadToCommandResult(packet, result))
                        {
                            std::cerr << "控制结果解析失败" << std::endl;
                            close(client_fd);
                            return -1;
                        }

                        if (result.result == robot_monitor::COMMAND_SUCCESS)
                        {
                            std::cout << "\n控制命令执行成功" << std::endl;
                            std::cout << "robot_id = " << result.robot_id << std::endl;
                            std::cout << "command_sequence = " << result.command_sequence << std::endl;
                        }

                        else if (result.result == robot_monitor::COMMAND_ROBOT_OFFLINE)
                        {
                            std::cout << "\n控制失败:Robot " << result.robot_id << " 在等待时间内仍未上线" << std::endl;
                        }
                        else if (result.result == robot_monitor::COMMAND_EXECUTE_FAILED)
                        {
                            std::cout << "\n控制失败：Robot "
                                      << result.robot_id
                                      << " 没有可用的cmd_vel订阅者"
                                      << std::endl;
                        }
                        
                    }

                    if (packet.header.type == robot_monitor::MSG_TEXT)
                    {
                        std::string text = robot_monitor::payloadToString(packet);

                        std::cout << "\nGateway消息：" << text << std::endl;
                    }

                    recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + consumed);
                }
            }
            else if (len == 0)
            {
                std::cout << "RobotGateway断开连接" << std::endl;
                break;
            }
            else
            {
                if (errno == EINTR)
                {
                    continue;
                }

                perror("recv");

                break;
            }
        }
    }

    close(client_fd);
    return 0;
}
