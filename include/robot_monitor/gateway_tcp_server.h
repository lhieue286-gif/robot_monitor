#ifndef ROBOT_MONITOR_GATEWAY_TCP_SERVER_H
#define ROBOT_MONITOR_GATEWAY_TCP_SERVER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include "robot_monitor/protocol.h"
// 管TCP -> 客户端的代码
// 2.连接客户端将TCP报文格式 -> 发布到客户端
namespace robot_monitor
{
class GatewayTcpServer
{
public:
    GatewayTcpServer();
    ~GatewayTcpServer();
    // 创建监听socket、绑定IP/端口,并加入epoll
    bool start(const std::string& ip,uint16_t port);

    //处理一次epoll事件.timeout_ms单位:毫秒
    void pollOnce(int timeout_ms);

    //将一个已经序列化好的TCP Packet广播给所有的客户端
    void broadcast(const std::vector<uint8_t>& packet);

    std::size_t clientCount()const;
private:

    static constexpr std::size_t MAX_SEND_BUFFER_SIZE = 4 * 1024 * 1024;
    struct Connection
    {
        // TCP文件描述符
        int fd = -1;
        // 接收缓冲区和fd对应 TCP接收缓冲区
        std::vector<uint8_t> recv_buffer; 
        // 保存等待发送的数据 TCP发送缓冲区
        std::vector<uint8_t> send_buffer;
        // 前面已经成功发送了多少字节，size_t 表示 大小、长度、数量、索引”的无符号正整数类型，ssize_t：有符号，可以表示负数
        std::size_t send_offset = 0; 
        // 是否已经完成身份注册
        bool registered = false;
        // 客户端身份
        ClientRole role = ROLE_UNKNOWN;

        // 如果是机器人，记录机器人ID
        int32_t robot_id = -1;

        // 这条连接最后一次收到心跳的时间
        std::chrono::steady_clock::time_point last_heartbeat;
    };

    struct PendingControlCommand
    {
        // 是哪个Monitor发来的
        int monitor_fd;

        // 控制哪台机器人
        int32_t robot_id;

        // 原控制命令的sequence
        uint32_t command_sequence;

        // 已经序列化好的SET_VELOCITY Packet
        std::vector<uint8_t> packet;

        // 最晚等待到什么时候
        std::chrono::steady_clock::time_point deadline;
    };

    enum class FlushResult
    {
        COMPLETE, // 全部发送完成
        PENDING,  // 暂时发不完，遇到EAGAIN
        ERROR     // 真正发生错误
    };

    int listen_fd_; // 定义一个监听的fd用于监听是不是有客户端连接
    int epoll_fd_;  // 定义一个epoll的fd用于将监听的fd和发送数据的fd交给epoll管理
    uint32_t server_sequence_;
    std::unordered_map<int, Connection> connections_; // 定义了一个名字是connections哈希表 规定里面保存的数据格式是 键值是int 数据是Connection
    /**
     * robot_id -> client fd
     * int32_t 对应：conn.robot_id
     * int 就是socket文件描述符：fd
     */
    std::unordered_map<int32_t, int> robot_connections_;
    std::vector<PendingControlCommand> pending_control_commands_; // 将所有的时间装入进去
    static constexpr bool ENABLE_PACKET_DEBUG = false; // 判断协议是不是打开
    static constexpr bool ENABLE_LEN_DEBUG = false;
    bool setNonBlocking(int fd); // 将fd设置非阻塞的函数

    /**
     * queueSend --> flushSendBuffer --> updateClientEvents
     *      |                |                     |
     *     数据      现在能发多少就发送多少   如果没有发完继续通知我继续发
     */
    FlushResult flushSendBuffer(Connection &conn);                      // 实际执行的非阻塞发送函数
    bool updateClientEvents(const Connection &conn);                    // 如果没有发送完通知我继续发送
    bool queueSend(Connection &conn, const std::vector<uint8_t> &data); // 将数据放到待发送的队列
    void sendToMonitors(const std::vector<uint8_t> &packet);            // 将机器人状态只发送给监控客户端

    bool parsePackets(Connection &conn); // 解析客户端的数据

    void acceptClient();                            // 当客户端连接成功,接受客户端的连接请求，并获得一个用于和该客户端通信的 client_fd。
    void handleClientEvent(int fd, uint32_t event); // 当客户端连接成功后处理客户端发送的数据
    void closeClient(int fd);                       // 关闭文件描述符
    void shutdown();                                // 关闭整个服务器

    void processPendingControlCommands();

    void processHeartbeatTimeouts(); // 记录心跳时间

    Connection *findRobot(int32_t robot_id); // 根据 robot_id 找机器人的函数
};
    
} // namespace robot_monitor
#endif