#ifndef ROBOT_MONITOR_SRC_PROTOCO_H
#define ROBOT_MONITOR_SRC_PROTOCO_H


#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

/***********************
 * TCP自定义报文格式，并将主机字节流转化为网络字节流，
 * 并将数据转化为序列化自己定义的报文格式
 * 在将报文格式转化为数据形式
 */

namespace robot_monitor
{
    /*
        ┌────────────── Header ──────────────┬──── payload ────┐
        │ magic │ type │ length │ sequence   │      data       │
        │ 2B    │ 2B   │ 4B     │ 4B         │    length B     │
        └────────────────────────────────────┴─────────────────┘

     */
    /**
     * 协议魔数，用来判断受到的数据是不是我们的协议，约定每一个数据包的前两个字节都是0xabcd
     * magic: 0xabcd
     */
    const uint16_t PACKET_MAGIC = 0xabcd;

    /**
     * 协议头固定长度
     * magic   2字节
     * type    2字节
     * length  4字节
     * sequence 4字节
     * 总共12字节
     */
    const std::size_t PACKET_HEADER_SIZE = 12;

    //防止lengeth异常导致服务器认为要接受几个GB的数据
    const uint32_t MAX_PAYLOAD_SIZE = 1024 * 1024; //1MB

    /**
     * MessageType: 消息类型
     * 也就是type字段的值,消息类型编号
     */
    enum MessageType
    {

        MSG_TEXT = 1,
        //后面项目继续扩展
        MSG_HEARTBEAT = 2, // 定义心跳数据定时检查机器人是否在线
        MSG_ROBOT_STATE = 3, //单机器人实现通信
        MSG_SET_VELOCITY =4, // 判断监控客户端发送的控制指令
        MSG_RESET_ROBOT =5,
        MSG_REGISTER = 6,   // 身份注册
        MSG_COMMAND_RESULT = 7, // Gateway给Monitor回复控制结果
        MSG_ROBOT_STATUS = 8, // 机器人在线状态消息
        MSG_STATE_STREAM_REQUEST = 9 // 监控客户端请求数据
    };
    

    // 一个 TCP 连接建立后，必须先 REGISTER 告诉服务器自己的身份。
    enum ClientRole : uint16_t
    {
        ROLE_UNKNOWN = 0, // 客户端身份未知，还没有完成身份注册。
        ROLE_ROBOT = 1,  // 这个 TCP 客户端是一台机器人。
        ROLE_MONITOR = 2 // 这个 TCP 客户端是监控端/控制端。
    };
    // 发送控制指令检查机器人是否在线
    enum CommandResultCode : uint16_t
    {
        COMMAND_SUCCESS = 0, // 在线和等待时间后成功连接

        // 等待超时后机器人仍然不在线
        COMMAND_ROBOT_OFFLINE = 1,
        COMMAND_EXECUTE_FAILED = 2 // 没有这个订阅者
    };
    // 机器人状态 
    enum RobotStatusCode : uint16_t
    {
        ROBOT_OFFLINE = 0,
        ROBOT_ONLINE = 1 //在线
    };

    /**
     * 协议头
     * 注意 ：这个结构体只是程序的内部表示
     * 不字节吧整个结构体send出去，因为不同编译器对结构体的内存对齐方式不一样
     */
    struct PacketHeader
    {
        uint16_t magic;    // 魔数，用来判断收到的数据是不是我们的协议,PACKET_MAGIC = 0xabcd
        uint16_t type;     // 消息类型,MessageType
        uint32_t length;   // 消息体长度,表示的是 Payload 长度，也就是传输数据的长度，不包括头部的长度,处理TCP 粘包/半包关键的字段。
        uint32_t sequence; // 消息序列号，用于标识消息的顺序
    };

    // 发送控制指令的时候检查机器人是不是在线反馈给监控客户端的数据结构体
    struct CommandResultData
    {
        int32_t robot_id;
        // 对应Monitor原来的控制命令sequence
        uint32_t command_sequence;
        uint16_t result;
    };

    // 检查机器人在线和断线的数据结构体
    struct RobotStatusData
    {
        int32_t robot_id;
        uint16_t status;
    };

    // 监控客户端请求数据 结构体
    struct StateStreamRequestData
    {
        int32_t robot_id;

        // 持续发送多少毫秒
        // 0表示立即停止
        uint32_t duration_ms;
    };
    // 监控客户端发送控制指令
    struct SetVelocityData
    {
        int32_t robot_id;
        double linear_velocity;
        double angular_velocity;
    };
    // 心跳检测的结构体
    struct HeartbeatData
    {
        int32_t robot_id;
    };

    //完整的数据包，包括头和消息体
    struct Packet
    {
        PacketHeader header;
        std::vector<uint8_t> payload; // 消息体
    };

    // 网络层机器人状态结构体
    struct RobotStateData
    {
        int32_t robot_id;

        double x;
        double y;
        double yaw;

        double linear_velocity;
        double angular_velocity;

        double battery;

        uint32_t timestamp_sec;
        uint32_t timestamp_nsec;
    };
    // REGISTER payload: role(2B) + robot_id(4B)
    struct RegisterData
    {
        ClientRole role = ROLE_UNKNOWN;
        int32_t robot_id = -1;
    };

    // 解析结果
    enum ParseResult
    {
        PARSE_OK,          // 解析成功
        PARSE_NEED_MORE,   //数据还没收完整
        PARSE_ERROR,       // 解析失败
    };

    //将一个Packet序列化成可以通过TCP发送的字节流
    std::vector<uint8_t> serializePacket(uint16_t type, uint32_t sequence, const std::vector<uint8_t>& payload);
    //字符串版本，方便测试
    std::vector<uint8_t> serializeTextPacket( uint32_t sequence, const std::string& text); 
    // 注册消息序列化
    std::vector<uint8_t> serializeRegisterPacket(uint32_t sequence,const RegisterData& data);      
    // 将机器人状态序列化成TCP Packet
    std::vector<uint8_t> serializeRobotStatePacket(uint32_t sequence,const RobotStateData &state);
    // 监控客户端发送控制指令的序列化函数
    std::vector<uint8_t> serializeSetVelocityPacket(uint32_t sequence,const SetVelocityData& data);
    //当机器人不在线的时候将不在线发送到监控客户端中
    std::vector<uint8_t> serializeCommandResultPacket(uint32_t sequence,const CommandResultData& data);
    // 检查机器人在线和断线的数据结构体 序列化函数
    std::vector<uint8_t> serializeRobotStatusPacket(uint32_t sequence,const RobotStatusData& data);
    // 监控客户端请求数据 结构体 序列化函数
    std::vector<uint8_t> serializeStateStreamRequestPacket(uint32_t sequence,const StateStreamRequestData& data);
    // 心跳检测序列化函数
    std::vector<uint8_t> serializeHeartbeatPacket(uint32_t sequence,const HeartbeatData& data);


    // 注册消息反序列化 
    bool payloadToRegister(const Packet& packet, RegisterData& data);
    // 将MSG_ROBOT_STATE的payload解析成机器人状态
    bool payloadToRobotState(const Packet &packet,RobotStateData &state);
    // 监控客户端发送控制指令的反序列化函数
    bool payloadToSetVelocity(const Packet& packet,SetVelocityData& data);
    // 接收中心客户端发送的机器人不在线的消息
    bool payloadToCommandResult(const Packet& packet,CommandResultData& data);
    // 检查机器人在线和断线的数据结构体 反序列化函数
    bool payloadToRobotStatus(const Packet& packet,RobotStatusData& data);
    // 监控客户端请求数据 结构体 反序列化函数
    bool payloadToStateStreamRequest(const Packet& packet,StateStreamRequestData& data);
    // 心跳检测反序列函数
    bool payloadToHeartbeat(const Packet& packet,HeartbeatData& data);

    /**
     * 尝试从buffer中解析出一个完整的数据包
     * buffer：TCP接受缓冲区
     * packer：解析成功后，把完整Packer放进去
     * consumed:如果解析成功，表示buffer中有多少字节被解析了
     * 返回值：解析结果
     */
    ParseResult tryParsePacket(const std::vector<uint8_t>& buffer, Packet& packet, std::size_t& consumed);
    //将payload序列化成字节流,方便MSG_TEXT消息测试
    std::string payloadToString(const Packet& packet);

} // namespace robot_monitor

#endif //ROBOT_MONITOR_SRC_PROTOCO_H