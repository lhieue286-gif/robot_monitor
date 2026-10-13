#include "robot_monitor/gateway_tcp_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "robot_monitor/protocol.h"

// TCP -> 客户端的代码
// 2.连接客户端将TCP报文格式 -> 发布到客户端

namespace robot_monitor
{
    GatewayTcpServer::GatewayTcpServer() : listen_fd_(-1), epoll_fd_(-1),server_sequence_(1)
    {
    }
    GatewayTcpServer::~GatewayTcpServer()
    {
        shutdown();
    }

    // 将fd设置非阻塞的函数
    bool GatewayTcpServer::setNonBlocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0); // F_GETFL:获取文件描述符fd的状态将状态保存到flags
        if (flags == -1)
        {
            perror("fcntl F_GETFL");
            return false;
        }

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) // F_SETFL：将文件描述符的状态在原来的属性基础上，加上 O_NONBLOCK 非阻塞属性
        {
            perror("fcntl FF_SETFL");
            return false;
        }
        return true;
    }
    // 创建监听socket、绑定IP/端口,并加入epoll
    bool GatewayTcpServer::start(const std::string &ip, uint16_t port)
    {
        // 1.创建套接字
        // 一个监听 socket 对应一个 listen_fd；每建立一个客户端连接，accept() 都会返回一个新的 client_fd
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0); // 创建一个TCP套接字，返回套接字文件描述符fd
        if (listen_fd_ == -1)
        {
            perror("socket error");
            return false;
        }
        int opt = 1;
        if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
        {
            perror("setsockopt SO_REUSEADDR");
            shutdown();
            return false;
        }
        // 2.设置服务器的IP和端口
        struct sockaddr_in server_addr{};                     // 用来保存 IPv4 地址信息的结构体变量
        server_addr.sin_family = AF_INET;                     // 设置地址族为IPv4
        server_addr.sin_port = htons(port);                   // 设置端口号为8080，htons函数将主机字节序转换为网络字节序
        // server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // 服务器的IP地址为127.0.0.1，表示只接受本机的连接请求。
        // ip.c_str() 将ip转化为const char*
        if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr.s_addr) != 1)
        {
            std::cerr << "非法服务器IP: " << ip << std::endl;
            shutdown();
            return false;
        }
        // 将套接字与IP地址和端口号绑定，bind函数将套接字fd与指定的IP地址和端口号绑定
        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == -1)
        {
            perror("bind error");
            shutdown();
            return false;
        }
        // 3.监听客户端的连接请求,监听套接字fd，等待客户端的连接请求，第二个参数表示最大连接数为128
        if (listen(listen_fd_, 128) == -1)
        {
            perror("listen error");
            shutdown();
            return false;
        }
        // ET模式要求socket使用非阻塞方式。
        if (!setNonBlocking(listen_fd_))
        {
            shutdown();
            return false;
        }

        // 创建一个文件描述符管理器
        epoll_fd_ = epoll_create1(0);

        if (epoll_fd_ == -1)
        {
            perror("epoll_create1 error");
            shutdown();
            return false;
        }

        // 将listen_fd注册到epoll

        struct epoll_event listen_event{}; // 创建一个 epoll 事件描述对象用并且设置监控规则
        // listen_event.events = EPOLLIN; // LT（Level Triggered，水平触发）模式，因为 LT 是 epoll 的默认模式。
        listen_event.events = EPOLLIN | EPOLLET; // 将 listen_event 修改为 ET，Edge Trigger，边沿触发。
        listen_event.data.fd = listen_fd_;       // 监控哪一个文件描述符

        /**
         * EPOLL_CTL_ADD 表示 把这个 listen_fd 加入 epoll_fd 的监控集合。
         * epoll_ctl(epoll_fd,EPOLL_CTL_ADD,listen_fd,&listen_event)：
         * 将监控的文件描述符告诉epoll_fd我要监控哪个fd,以及按照listen_event 里写的规则进行监控
         */
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &listen_event) == -1)
        {
            perror("epoll_ctl listen_fd_");
            shutdown();
            return false;
        }

        std::cout << "中心服务器启动 IP =  " << ip << ",端口 :" << port << std::endl;
        return true;
    }

    // 实际执行的非阻塞发送函数
    GatewayTcpServer::FlushResult GatewayTcpServer::flushSendBuffer(Connection &conn)
    {
        while (conn.send_offset < conn.send_buffer.size())
        {
            ssize_t n = send(
                conn.fd,
                conn.send_buffer.data() + conn.send_offset,
                conn.send_buffer.size() - conn.send_offset,
                MSG_NOSIGNAL); // 返回值是发送的字节数

            if (n > 0)
            {
                conn.send_offset += static_cast<std::size_t>(n);
                std::cout << "fd = " << conn.fd
                          << " 本次send发送 " << n
                          << " 字节，当前进度 " << conn.send_offset
                          << "/" << conn.send_buffer.size() << std::endl;
                continue;
            }
            if (n == -1 && errno == EINTR) // 理解这些错误码都是什么意思
            {
                continue;
            }
            if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                std::cout << "fd = " << conn.fd << " 当前发送缓冲区已满,等待EPOLLOUT" << std::endl;
                return FlushResult::PENDING;
            }

            if (n == -1)
            {
                perror("send error");
                return FlushResult::ERROR;
            }
            if (n == 0)
            {
                std::cerr << "fd = " << conn.fd << " send返回0" << std::endl;
                return FlushResult::ERROR;
            }
        }
        // 能运行到这里，说明send_buffer全部发送完成
        conn.send_buffer.clear();
        conn.send_offset = 0;

        return FlushResult::COMPLETE;
    }
    // 如果没有发送完通知我继续发送
    bool GatewayTcpServer::updateClientEvents(const Connection &conn)
    {
        struct epoll_event event{};
        event.data.fd = conn.fd;                       // 这个是不是建立连接之后的fd
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET; // 这个理解一下

        // 如果还有数据没发完
        // 就额外监听EPOLLOUT
        if (conn.send_offset < conn.send_buffer.size())
        {
            event.events |= EPOLLOUT;
        }
        /**
         * EPOLL_CTL_ADD 添加一个 fd 到 epoll。
         * EPOLL_CTL_MOD 修改已经加入 epoll 的 fd 的监听事件。
         * EPOLL_CTL_DEL 删除这个 fd，不再监听。
         */
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn.fd, &event) == -1)
        {
            perror("epoll_ctl MOD client");
            return false;
        }

        return true;
    }
    // 将数据放到待发送的队列
    bool GatewayTcpServer::queueSend(Connection &conn, const std::vector<uint8_t> &data)
    {
        if (conn.send_buffer.size() + data.size() > MAX_SEND_BUFFER_SIZE)
        {
            std::cerr << "fd = " << conn.fd
                      << " 发送缓冲区过大"
                      << std::endl;

            return false;
        }

        // 将数据装入对应客户端fd的发送缓冲区里面
        conn.send_buffer.insert(conn.send_buffer.end(), data.begin(), data.end());

        // 尝试立即发送
        FlushResult result = flushSendBuffer(conn);
        // 真正发送错误
        if (result == FlushResult::ERROR)
        {
            return false;
        }
        // COMPLETE：
        // send_buffer已经空了，不需要EPOLLOUT
        //
        // PENDING：
        // send_buffer还有数据，需要EPOLLOUT
        //
        // 两种情况统一通过updateClientEvents修改
        return updateClientEvents(conn);
    }

    // 只是发送给监控客户端的
    void GatewayTcpServer::sendToMonitors(const std::vector<uint8_t>& packet)
    {
        std::vector<int> failed_fds;

        for(auto& item : connections_)
        {
            Connection& conn = item.second;
            // 没有注册的客户端不发送
            if (!conn.registered)
            {
                continue;
            }
            // 只发送给监控客户端
            if (conn.role != ROLE_MONITOR)
            {
                continue;
            }
            if (!queueSend(conn,packet))
            {
                std::cerr
                    << "向监控客户端 fd = "
                    << conn.fd
                    << " 发送失败"
                    << std::endl;

                failed_fds.push_back(conn.fd);
            }
        }
        // 不要在遍历 connections_ 的过程中直接 erase
        for (int fd : failed_fds)
        {
            closeClient(fd);
        }
    }

    //中心服务器解析客户端的数据
    bool GatewayTcpServer::parsePackets(Connection &conn)
    {
        while (true)
        {
            Packet packet{}; // 定义协议包
            std::size_t consumed = 0;

            /**
             * 将conn接收缓冲区的解析成 Header  和  payload
             * 将payload 保存在 packet
             */
            const ParseResult result = tryParsePacket(conn.recv_buffer, packet, consumed); 

            if (result == PARSE_NEED_MORE)
            {
                // 注意：
                // 这里不能clear recv_buffer
                // 因为这些数据下一次recv还要继续使用
                // 半包：保留recv_buffer，等待下一次recv继续补齐。
                return true;
            }

            // 协议错误
            if (result == PARSE_ERROR)
            {
                std::cerr << "fd = " << conn.fd << " 收到非法Packet" << std::endl;
                return false;
            }

            if (ENABLE_PACKET_DEBUG && packet.header.type != MSG_HEARTBEAT)
            {
                std::cout << "解析成功一个Packet" << std::endl;
                std::cout << "magic = 0x" << std::hex << packet.header.magic << std::dec << std::endl;
                std::cout << "type = " << packet.header.type << std::endl;
                std::cout << "length = " << packet.header.length << std::endl;
                std::cout << "sequence = " << packet.header.sequence << std::endl;
            }

            // 客户端还没有注册时，只允许发送 MSG_REGISTER
            if (!conn.registered && packet.header.type != MSG_REGISTER)
            {
                std::cerr << "fd = " << conn.fd
                          << " 尚未注册，第一条消息必须是 MSG_REGISTER"
                          << std::endl;

                return false;
            }

            // 中心服务器收到身份注册的数据
            if (packet.header.type == MSG_REGISTER) 
            {
                RegisterData register_data;
                /**
                 * 1. 解析REGISTER payload 
                 * RegisterData 数据类型 
                 * ROLE_UNKNOWN 未注册的身份
                 * ROLE_ROBOT   机器人
                 * ROLE_MONITOR 监控客户端
                 * robot_id
                 */
                if (!payloadToRegister(packet,register_data))
                {
                    std::cerr << "fd = " << conn.fd << " 注册消息解析失败" << std::endl;
                    return false;
                }
                // 2. 不允许同一个 TCP 连接重复注册
                if (conn.registered)
                {
                    std::cerr << "fd = "
                              << conn.fd
                              << " 重复注册"
                              << std::endl;

                    return false;
                }
                // 3. 判断客户端身份是否合法
                if (register_data.role != ROLE_ROBOT && register_data.role != ROLE_MONITOR)
                {
                    std::cerr << "fd = " << conn.fd
                              << " 注册身份非法"
                              << std::endl;

                    return false;
                }
                // 4. 如果是机器人，robot_id 必须有效
                if (register_data.role == ROLE_ROBOT)
                {
                    if (register_data.robot_id < 0)
                    {
                        std::cerr << "robot_id非法" << std::endl;
                        return false;
                    }
                    // 检查 robot_id 是否已经被占用
                    auto it = robot_connections_.find(register_data.robot_id);
                    if (it != robot_connections_.end())
                    {
                        std::cerr << "robot_id = "
                                  << register_data.robot_id
                                  << " 已存在"
                                  << std::endl;

                        return false;
                    }
                }

                // 5. 注册成功，把身份保存到这个 Connection
                conn.registered = true;
                conn.role = register_data.role;
                conn.robot_id = register_data.robot_id;

                // 6. 打印注册结果
                if (conn.role == ROLE_ROBOT)
                {
                    robot_connections_[conn.robot_id] = conn.fd;
                    // 注册成功后给机器人3秒发送第一次心跳
                    conn.last_heartbeat = std::chrono::steady_clock::now();
                    std::cout << "机器人注册成功"
                              << ", fd = " << conn.fd
                              << ", robot_id = "
                              << conn.robot_id
                              << std::endl;
                    RobotStatusData status_data{};
                    std::cout << "================"<<std::endl;

                    status_data.robot_id = conn.robot_id;
                    status_data.status = ROBOT_ONLINE;

                    std::vector<uint8_t> status_packet = serializeRobotStatusPacket(server_sequence_++,status_data);

                    // 将机器人注册消息发送给客户端
                    sendToMonitors(status_packet); //还没解析
                }
                if (conn.role == ROLE_MONITOR)
                {
                    std::cout << "监控客户端"
                              << "fd = " << conn.fd 
                              << "注册成功" 
                              << ",role = MONITOR"
                              << std::endl;
                    // 1.回复Monitor自身注册成功
                    std::vector<uint8_t> register_reply = serializeTextPacket(server_sequence_++,"监控客户端注册成功");

                    if (!queueSend(conn,register_reply))
                    {
                        std::cerr << "发送Monitor注册结果失败" << std::endl;
                        return false;
                    }

                    // 2. 告诉新Monitor当前有哪些机器人在线
                    for(const auto& robot_item : robot_connections_)
                    {
                        const int32_t robot_id = robot_item.first;
                        RobotStatusData status_data{};
                        status_data.robot_id = robot_id;
                        status_data.status = ROBOT_ONLINE;
                        std::vector<uint8_t> status_packet = serializeRobotStatusPacket(server_sequence_++,status_data);
                        if (!queueSend(conn,status_packet))
                        {
                            std::cerr << "发送在线机器人列表失败" << std::endl;
                            return false;
                        }
                    }
                    std::cout << "================"<<std::endl;
                }
            }
            if (packet.header.type == MSG_HEARTBEAT)
            {
                if (conn.role != ROLE_ROBOT)
                {
                    std::cerr << "非机器人客户端不能发送心跳" << std::endl;

                    return false;
                }
                HeartbeatData heartbeat{};

                if (!payloadToHeartbeat(packet,heartbeat))
                {
                    std::cerr << "心跳消息解析失败" << std::endl;

                    return false;
                }
                // 防止Robot1发送robot_id=2的假心跳
                if (heartbeat.robot_id != conn.robot_id)
                {
                    std::cerr
                        << "心跳robot_id不一致"
                        << ", 注册robot_id = "
                        << conn.robot_id
                        << ", 心跳robot_id = "
                        << heartbeat.robot_id
                        << std::endl;

                    return false;
                }
                conn.last_heartbeat = std::chrono::steady_clock::now();
            }
            

            // 中心服务器收到监控客户端的控制指令
            if (packet.header.type == MSG_SET_VELOCITY)
            {
                if (conn.role != ROLE_MONITOR)
                {
                    std::cerr
                        << "fd = " << conn.fd
                        << " 非监控客户端不能发送控制命令"
                        << std::endl;

                    return false;
                }
                /**
                 * SetVelocityData数据类型
                 * robot_id;
                 * linear_velocity;
                 *  angular_velocity;
                 */
                SetVelocityData command;

                if (!payloadToSetVelocity(packet,command))
                {
                    std::cerr
                        << "SET_VELOCITY解析失败"
                        << std::endl;

                    return false;
                }

                std::cout << "================"<<std::endl;
                std::cout << "收到速度控制命令" << std::endl;
                std::cout << "robot_id = " << command.robot_id << std::endl;
                std::cout << "linear = " << command.linear_velocity << std::endl;
                std::cout << "angular = " << command.angular_velocity << std::endl;

                // 查 robot_id → fd
                Connection *robot_conn = findRobot(command.robot_id);
                if (robot_conn == nullptr) // 当机器人不在线的时候
                {
                    std::cout
                        << "机器人当前不在线，等待上线"
                        << ", robot_id = "
                        << command.robot_id
                        << std::endl;

                    std::vector<uint8_t> forward_packet =
                        serializePacket(
                            packet.header.type,
                            packet.header.sequence,
                            packet.payload);

                    PendingControlCommand pending; // 重新创建结构体

                    pending.monitor_fd = conn.fd;
                    pending.robot_id = command.robot_id;
                    pending.command_sequence = packet.header.sequence;
                    pending.packet = std::move(forward_packet);

                    // 最多等待2秒
                    pending.deadline =std::chrono::steady_clock::now() + std::chrono::seconds(2);

                    pending_control_commands_.push_back(std::move(pending)); // 当机器人不在线时候将这些数据装入进去
                }
                else
                { // 当机器人在线的时候
                    std::vector<uint8_t> forward_packet =
                        serializePacket(
                            packet.header.type,
                            packet.header.sequence,
                            packet.payload);

                    // 将控制指令发送到机器人服务端
                    if (!queueSend(*robot_conn, forward_packet)) //将控制指令发送出去
                    {
                        std::cerr
                            << "发送控制命令失败"
                            << std::endl;
                    }
                }
            }

            // 中心服务器收到监控客户端的状态请求数据
            if (packet.header.type == MSG_STATE_STREAM_REQUEST)
            {
                // 检查是不是监控客户端
                if (conn.role != ROLE_MONITOR )
                {
                    std::cerr << "非Monitor客户端不能请求机器人状态" << std::endl;

                    return false;
                }
                /**
                 * robot_id;
                 * duration_ms;
                 */
                StateStreamRequestData request{};

                if (!payloadToStateStreamRequest(packet,request)) // 解析请求机器人客户端状态指令
                {
                    std::cerr << "状态发送请求解析失败" << std::endl;

                    return false;
                }
                if (request.robot_id < 0)
                {
                    std::cerr << "robot_id非法" << std::endl;

                    return false;
                }
                if (request.duration_ms > 30000)
                {
                    std::cerr << "状态查看时间不能超过30秒" << std::endl;

                    return false;
                }
                std::cout << "收到状态查看请求" << std::endl;
                std::cout << "robot_id = " << request.robot_id << std::endl;
                std::cout << "duration_ms = " << request.duration_ms << std::endl;

                // 寻找这个 robot_id 

                Connection* robot_conn = findRobot(request.robot_id);
                if (robot_conn == nullptr)
                {
                    std::cout << "机器人不在线" << ", robot_id = " << request.robot_id << std::endl;

                    RobotStatusData status_data{};

                    status_data.robot_id = request.robot_id;
                    status_data.status = ROBOT_OFFLINE;
                    std::vector<uint8_t> status_packet = serializeRobotStatusPacket(server_sequence_++,status_data);
                    if (!queueSend(conn, status_packet))
                    {
                        std::cerr << "发送在线机器人列表失败" << std::endl;
                        return false;
                    }
                }else{
                    // 保持原来的type、sequence和payload
                    std::vector<uint8_t> forward_packet =
                        serializePacket(
                            packet.header.type,
                            packet.header.sequence,
                            packet.payload); //报文还是请求状态的格式

                    if (!queueSend(*robot_conn,forward_packet)) // 向机器人发送状态请求
                    {
                        std::cerr << "向机器人转发状态请求失败"
                                  << ", robot_id = "
                                  << request.robot_id
                                  << std::endl;
                    }
                    else
                    {
                        std::cout << "状态查看请求已转发给机器人"
                                  << ", robot_id = "
                                  << request.robot_id
                                  << std::endl;
                    }
                }
            }
            
            // 中心服务器收到机器人发布的状态消息，并发送给客户端
            if (packet.header.type == MSG_ROBOT_STATE) // 机器人的状态的解析
            {

                if (conn.role != ROLE_ROBOT)
                {
                    std::cerr << "fd = " << conn.fd
                              << " 非机器人客户端不能发送 MSG_ROBOT_STATE"
                              << std::endl;

                    return false;
                }

                RobotStateData state;
                if (!payloadToRobotState(packet, state))
                {
                    std::cerr << "fd = " << conn.fd
                              << " RobotState解析失败"
                              << std::endl;

                    return false;
                }
                // 检查这个状态是不是当前连接对应的机器人
                if (state.robot_id != conn.robot_id)
                {
                    std::cerr << "robot_id不一致"
                              << ", 注册robot_id = " << conn.robot_id
                              << ", 状态robot_id = " << state.robot_id
                              << std::endl;

                    return false;
                }

                std::cout << "==================" << std::endl;
                std::cout << "robot_id = " << state.robot_id << std::endl;
                std::cout << "x = " << state.x << std::endl;
                std::cout << "y = " << state.y << std::endl;
                std::cout << "yaw= " << state.yaw << std::endl;
                std::cout << "linear_velocity = " << state.linear_velocity << std::endl;
                std::cout << "angular_velocity = " << state.angular_velocity  << std::endl;
                std::cout << "battery = " << state.battery << std::endl;

                // 将解析后的 Packet 重新变成 TCP 字节流
                std::vector<uint8_t> forward_packet =
                    serializePacket(
                        packet.header.type,
                        packet.header.sequence,
                        packet.payload);

                // 只发送给监控客户端
                sendToMonitors(forward_packet);
            }

            if (packet.header.type == MSG_COMMAND_RESULT)
            {
                // 只有机器人可以回复执行结果
                if (conn.role != ROLE_ROBOT)
                {
                    std::cerr << "非机器人客户端不能发送控制结果" << std::endl;
                    return false;
                }
                CommandResultData command_result{};
                if (!payloadToCommandResult(packet, command_result))
                {
                    std::cerr << "控制结果解析失败" << std::endl;
                    return false;
                }
                if (command_result.robot_id != conn.robot_id)
                {
                    std::cerr << "控制结果robot_id不一致"
                              << ", 注册robot_id = "
                              << conn.robot_id
                              << ", 结果robot_id = "
                              << command_result.robot_id
                              << std::endl;

                    return false;
                }

                std::cout << "收到机器人控制结果"
                          << ", robot_id = "
                          << command_result.robot_id
                          << ", command_sequence = "
                          << command_result.command_sequence
                          << ", result = "
                          << command_result.result
                          << std::endl;
                          
                // 重新组成Packet发送给Monitor
                std::vector<uint8_t> forward_packet =
                    serializePacket(
                        packet.header.type,
                        packet.header.sequence,
                        packet.payload);

                sendToMonitors(forward_packet);
            }
            

            // 测试TCP的发送和接收的代码（和机器人带没有关系）
            if (packet.header.type == MSG_TEXT)
            {
                std::string text = payloadToString(packet);
                std::cout << "payload = " << text << std::endl;

                std::string reply_text =
                    "Server ACK, sequence = " + std::to_string(packet.header.sequence);
                std::vector<uint8_t> reply_packet =
                    serializeTextPacket(packet.header.sequence, reply_text);

                if (!queueSend(conn, reply_packet))
                {
                    return false;
                }
            }

            // std::cout << "==================" << std::endl;

            // 粘包：删掉已经解析完成的一个Packet，然后继续解析后面的数据。
            conn.recv_buffer.erase(
                conn.recv_buffer.begin(),
                conn.recv_buffer.begin() + consumed);
        }
    }

    // 当客户端连接成功,接受客户端的连接请求，并获得一个用于和该客户端通信的 client_fd。
    void GatewayTcpServer::acceptClient()
    {
        while (true)
        {
            struct sockaddr_in client_addr{};                // 创建一个结构体变量，用来保存客户端的IP地址和端口号
            socklen_t client_addr_len = sizeof(client_addr); // 保存客户端地址结构体的大小
            // 某个客户端对应的连接”的，是accept
            int client_fd = accept(
                listen_fd_,
                reinterpret_cast<sockaddr *>(&client_addr),
                &client_addr_len);

            if (client_fd == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 当前已经没有等待accept的连接
                    return;
                }

                if (errno == EINTR)
                {
                    continue;
                }

                perror("accept error");
                return;
            }

            // 后面处理新客户端
            // 在连接成功后，将客户端也设置非阻塞
            if (!setNonBlocking(client_fd))
            {
                close(client_fd);
                continue;
            }

            // 将 client_fd 也加入到epoll
            epoll_event client_event{};
            // client_event.events = EPOLLIN;
            /**
             * EPOLLIN      有数据可以读
             * EPOLLOUT     可以继续写数据
             * EPOLLERR     socket发生错误
             * EPOLLHUP     连接挂断
             * EPOLLRDHUP   对端关闭连接
             * EPOLLET      边沿触发
             */
            client_event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
            client_event.data.fd = client_fd;

            if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &client_event) == -1)
            {
                perror("epoll_ctl client_fd");
                close(client_fd);
                continue;
            }

            Connection conn;
            conn.fd = client_fd;
            connections_.emplace(client_fd, std::move(conn)); // 将连接客户端的client_fd与接收缓冲区绑定，方便查找对应的接收缓冲区

            char client_ip[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

            std::cout << "新客户端连接 fd = " << client_fd
                      << ", ip = " << client_ip
                      << ", 端口号 = " << ntohs(client_addr.sin_port)
                      << std::endl;
        }
    }

    // 当某个客户端建立连接并且已经建立起来的客户端发送数据，查找相应的客户端
    // fd发生了什么事件。理解一下
    void GatewayTcpServer::handleClientEvent(int fd, uint32_t event)
    {
        auto it = connections_.find(fd);
        if (it == connections_.end())
        {
            return;
        }
        /**
        * first        second
           ↓             ↓
          key           value
          fd         Connection
        */
        Connection &conn = it->second;
        bool need_close = false;

        if (event & EPOLLIN)
        {
            while (true)
            {
                uint8_t temp_buffer[4096];
                ssize_t len = recv(fd, temp_buffer, sizeof(temp_buffer), 0);

                if (len > 0)
                {
                    if (ENABLE_LEN_DEBUG)
                    {
                        std::cout << "fd = " << fd
                                  << ", 本次recv收到 "
                                  << len << " 字节" << std::endl;
                    }
                    conn.recv_buffer.insert(
                        conn.recv_buffer.end(),
                        temp_buffer,
                        temp_buffer + len);

                    if (!parsePackets(conn))
                    {
                        need_close = true;
                        break;
                    }
                    continue;
                }
                if (len == 0)
                {
                    std::cout << "==================" << std::endl;   
                    std::cout << "客户端 fd = " << fd << " 断开连接" << std::endl;
                    need_close = true;
                    break;
                }

                if (errno == EINTR)
                {
                    continue;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }

                perror("recv error");
                need_close = true;
                break;
            }
        }
        if (!need_close && (event & EPOLLOUT))
        {
            FlushResult result = flushSendBuffer(conn);
            if (result == FlushResult::ERROR || !updateClientEvents(conn))
            {
                need_close = true;
            }
        }

        if (!need_close && (event & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)))
        {
            std::cout << "fd = " << fd << " 出现关闭/异常事件" << std::endl;
            need_close = true;
        }

        if (need_close)
        {
            closeClient(fd);
        }
    }

    void GatewayTcpServer::processPendingControlCommands()
    {
        const auto now = std::chrono::steady_clock::now();

        auto it = pending_control_commands_.begin();

        while (it != pending_control_commands_.end())
        {
            // ==============================
            // 1. 先检查原Monitor还在不在
            // ==============================

            auto monitor_it =
                connections_.find(it->monitor_fd);

            if (monitor_it == connections_.end())
            {
                // Monitor已经断开
                // 这条待处理命令没有意义了
                it =
                    pending_control_commands_.erase(it);

                continue;
            }

            Connection &monitor_conn = monitor_it->second;

            // ==============================
            // 2. 看机器人有没有上线
            // ==============================

            Connection *robot_conn = findRobot(it->robot_id);

            if (robot_conn != nullptr)
            {
                std::cout
                    << "等待期间机器人已上线"
                    << ", robot_id = "
                    << it->robot_id
                    << std::endl;

                // 转发之前保存的控制命令
                if (queueSend(*robot_conn,it->packet))
                {
                    // 命令已经转交给机器人
                    // 删除待处理记录
                    it =  pending_control_commands_.erase(it);

                    continue;
                }
            }

            // ==============================
            // 3. 机器人还没上线，检查是否超时
            // ==============================

            if (now >= it->deadline)
            {
                CommandResultData result;

                result.robot_id = it->robot_id;
                result.command_sequence = it->command_sequence;
                result.result = COMMAND_ROBOT_OFFLINE;

                std::vector<uint8_t> result_packet = serializeCommandResultPacket( it->command_sequence, result);

                // 给Monitor通知一次
                queueSend(monitor_conn,result_packet);

                std::cout
                    << "机器人等待超时"
                    << ", robot_id = "
                    << it->robot_id
                    << ", 已通知Monitor"
                    << std::endl;

                // ===========================
                // 通知加入send_buffer后
                // 删除这条等待消息
                // ===========================

                it = pending_control_commands_.erase(it);

                continue;
            }

            ++it;
        }
    }

    // 执行epoll监控
    void GatewayTcpServer::pollOnce(int timeout_ms)
    {
        if (epoll_fd_ == -1)
        {
            return;
        }

        constexpr int MAX_EVENTS = 1024;       // constexpr： 这个值在编译阶段就能确定，是一个编译期常量。
        struct epoll_event events[MAX_EVENTS]; // 定义一个epoll事件数组，用来接收 epoll_wait() 返回的“哪些 fd 发生了哪些事件”
        /**
         * event_count 是epoll_wait返回的event_count个事件需要处理。
         * 如果有客户端连接epoll_wait就会将listen_fd的值存储到events，并且有客户端通信也会通知accept创建一个client_fd存储到events，并将这些东西放到
         * event_count中
         */
        int event_count = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);
        if (event_count == -1)
        {
            if (errno != EINTR)
            {
                perror("epoll_wait error");
            }
            return;
        }

        for (int i = 0; i < event_count; i++)
        {
            int fd = events[i].data.fd;        // 哪个fd
            uint32_t event = events[i].events; // fd发生了什么事件。理解一下
            if (fd == listen_fd_)
            {
                acceptClient();
            }
            else
            {
                handleClientEvent(fd,event);
            }
        }

        // 每轮检查等待中的控制命令
        processPendingControlCommands();

        // 检查机器人心跳
        processHeartbeatTimeouts();
    }

    // 给所有的客户端发送函数
    void GatewayTcpServer::broadcast(const std::vector<uint8_t>& packet)
    {
        for (auto it = connections_.begin(); it != connections_.end();)
        {
            Connection& conn = it->second;
            int fd = conn.fd;
            if (!queueSend(conn,packet))
            {
                std::cerr << "向客户端 fd = " << fd << " 发送数据失败" << std::endl;
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
                it = connections_.erase(it);
                continue;
            }
            ++it;
        }
    }
    void GatewayTcpServer::processHeartbeatTimeouts()
    {
        const auto now = std::chrono::steady_clock::now();

        std::vector<int> timeout_fds;

        for (const auto &item : connections_)
        {
            const Connection &conn = item.second;

            if (!conn.registered)
            {
                continue;
            }

            if (conn.role != ROLE_ROBOT)
            {
                continue;
            }

            const auto elapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                now -
                conn.last_heartbeat);

            // 3秒没有收到心跳，认为机器人离线
            if (elapsed >= std::chrono::milliseconds(3000))
            {
                std::cerr
                    << "机器人心跳超时"
                    << ", robot_id = "
                    << conn.robot_id
                    << ", fd = "
                    << conn.fd
                    << ", elapsed_ms = "
                    << elapsed.count()
                    << std::endl;

                timeout_fds.push_back(
                    conn.fd);
            }
        }

        // 不能在遍历connections_时直接删除
        for (int fd : timeout_fds)
        {
            closeClient(fd);
        }
    }

    // 根据 robot_id 找机器人的函数
    GatewayTcpServer:: Connection* GatewayTcpServer::findRobot(int32_t robot_id)
    {
        //根据机器人来找这个机器人对应的那块空间
        auto robot_it = robot_connections_.find(robot_id);
        if (robot_it == robot_connections_.end())
        {
            return nullptr;
        }

        int fd = robot_it->second;
        auto conn_it = connections_.find(fd);
        if (conn_it == connections_.end())
        {
            return nullptr;
        }
        return &conn_it->second;
    }
    // std::unordered_map<int, Connection> connections_;取出函数
    std::size_t GatewayTcpServer::clientCount() const
    {
        return connections_.size();
    }
    // 关闭文件描述符
    void GatewayTcpServer::closeClient(int fd)
    {
        auto it = connections_.find(fd);
        if (it == connections_.end())
        {
            return;
        }

        bool need_notify_offline = false;
        int32_t offline_robot_id = -1;

        // 如果是已注册机器人，删除 robot_id -> fd
        if (it->second.registered && it->second.role == ROLE_ROBOT)
        {
            const int32_t robot_id = it->second.robot_id;
            auto robot_it = robot_connections_.find(robot_id);

            // 必须确认 robot_id 当前对应的就是这个 fd
            if (robot_it != robot_connections_.end() && robot_it->second == fd)
            {
                robot_connections_.erase(robot_it);

                offline_robot_id = robot_id;
                need_notify_offline = true;
            }
        }

        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        connections_.erase(it);
        std::cout << "已移除客户端连接,fd = " << fd << std::endl;

        // 连接清理完成后，再通知Monitor
        if (need_notify_offline)
        {
            std::cout
                << "机器人离线"
                << ", robot_id = "
                << offline_robot_id
                << ", fd = "
                << fd
                << std::endl;

            RobotStatusData status_data{};

            status_data.robot_id = offline_robot_id;

            status_data.status = ROBOT_OFFLINE;

            std::vector<uint8_t> status_packet =
                serializeRobotStatusPacket(
                    server_sequence_++,
                    status_data);

            sendToMonitors(status_packet);
        }
    }
    void GatewayTcpServer::shutdown()
    {
        for (auto &item : connections_)
        {
            close(item.first);
        }
        connections_.clear();
        robot_connections_.clear();

        if (epoll_fd_ != -1)
        {
            close(epoll_fd_);
            epoll_fd_ = -1;
        }

        if (listen_fd_ != -1)
        {
            close(listen_fd_);
            listen_fd_ = -1;
        }
    }

} // namespace robot_monitor
