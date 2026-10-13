#include "robot_monitor/protocol.h"
#include <cstring>
#include <arpa/inet.h>
#include <endian.h>

/***********************
 * TCP自定义报文格式，并将主机字节流转化为网络字节流，
 * 并将数据转化为序列化自己定义的报文格式
 * 在将报文格式转化为数据形式
 */

namespace robot_monitor
{
    /**
     * 向buffer中写入一个uint16_t类型的整数，转化成网络字节序
     */
    static void appendUint16(std::vector<uint8_t>& buffer, uint16_t value)
    {
        uint16_t network_value = htons(value); // 将主机字节序转换为网络字节序
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&network_value); // 将整数的地址转换为字节指针
        buffer.insert(buffer.end(), p, p + sizeof(network_value)); // 将字节插入到buffer的末尾
    }

    /**
     * 向buffer中写入一个uint32_t类型的整数，转化成网络字节序
     */
    static void appendUint32(std::vector<uint8_t>& buffer, uint32_t value)
    {
        uint32_t network_value = htonl(value); // 将主机字节序转换为网络字节序
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&network_value); // 将整数的地址转换为字节指针
        buffer.insert(buffer.end(), p, p + sizeof(network_value)); // 将字节插入到buffer的末尾
    }
    
    static void appendUint64(std::vector<uint8_t>& buffer,uint64_t value)
    {
        uint64_t network_value = htobe64(value);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&network_value);
        buffer.insert(buffer.end(),p,p + sizeof(network_value));
    }

    static void appendDouble(std::vector<uint8_t>& buffer,double value)
    {
        uint64_t bits = 0;
        std::memcpy(&bits,&value,sizeof(value));
        appendUint64(buffer,bits);
    }

    //从buffer中读取一个uint16_t类型的整数，并将其转换为主机字节序
    static uint16_t readUint16(const std::vector<uint8_t>& buffer, std::size_t offset)
    {
        uint16_t network_value;
        // 从 buffer 的 offset 位置开始，拷贝 sizeof(network_value) 个字节到 network_value 这个变量里。
        std::memcpy(&network_value, buffer.data() + offset, sizeof(network_value)); 
        return ntohs(network_value); // 将网络字节序转换为主机字节序
    }

    //从buffer中读取一个uint32_t类型的整数，并将其转换为主机字节序
    static uint32_t readUint32(const std::vector<uint8_t>& buffer, std::size_t offset)
    {
        uint32_t network_value;
        std::memcpy(&network_value, buffer.data() + offset, sizeof(network_value));
        return ntohl(network_value); // 将网络字节序转换为主机字节序
    }

    static uint64_t readUint64(const std::vector<uint8_t>& buffer,std::size_t offset)
    {
        uint64_t network_value = 0;
        std::memcpy(&network_value,buffer.data() + offset,sizeof(network_value));
        return be64toh(network_value);
    }
    static double readDouble(const std::vector<uint8_t>& buffer ,std::size_t offset)
    {
        uint64_t bits = readUint64(buffer,offset);
        double value = 0.0;
        std::memcpy(&value,&bits,sizeof(bits));
        return value;
    }

    //将一个Packet序列化成可以通过TCP发送的字节流
    std::vector<uint8_t> serializePacket(
        uint16_t type,
        uint32_t sequence,
        const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> buffer;
        buffer.reserve(PACKET_HEADER_SIZE + payload.size()); // 预分配内存，避免多次分配

        //header
        appendUint16(buffer, PACKET_MAGIC); // 写入魔数
        appendUint16(buffer, type);          // 写入消息类型
        appendUint32(buffer, static_cast<uint32_t>(payload.size())); // 写入消息体长度
        appendUint32(buffer, sequence);      // 写入消息序列号

        //payload
        buffer.insert(
            buffer.end(), 
            payload.begin(), 
            payload.end()); // 写入消息体数据

        return buffer; // buffer 就是 magic type length(消息的长度) sequence(第几次发送) payload是 id 位姿 速度 电量 时间
    }

    // 文字测试 
    std::vector<uint8_t> serializeTextPacket(
        uint32_t sequence,
        const std::string& text){

            std::vector<uint8_t> payload(
                text.begin(),
                text.end());

            return serializePacket(
                MSG_TEXT,
                sequence,
                payload);
        }

    // 将机器人状态序列化成TCP Packet
    std::vector<uint8_t> serializeRobotStatePacket(
        uint32_t sequence,
        const RobotStateData &state)
    {
        std::vector<uint8_t> payload;

        // robot_id
        appendUint32(payload,static_cast<uint32_t>(state.robot_id)); //static_cast强制类型转化

        //位姿
        appendDouble(payload,state.x);
        appendDouble(payload,state.y);
        appendDouble(payload,state.yaw);

        // 速度
        appendDouble(payload,state.linear_velocity);
        appendDouble(payload,state.angular_velocity);

        // 电量
        appendDouble(payload,state.battery);
        // 时间
        appendUint32(payload,state.timestamp_sec);
        appendUint32(payload,state.timestamp_nsec);
        return serializePacket(MSG_ROBOT_STATE , sequence ,payload); //payload是 id 位姿 速度 电量 时间 
    }
    // 包装结束 

    // 将客户端注册信息序列化成TCP Packet
    std::vector<uint8_t> serializeRegisterPacket(uint32_t sequence,const RegisterData& data)
    {
        std::vector<uint8_t> payload;
        // role占2字节
        appendUint16(payload,static_cast<uint16_t>(data.role));
        // robot_id占4字节
        appendUint32(payload,static_cast<uint32_t>(data.robot_id));
        return serializePacket(MSG_REGISTER,sequence,payload);
    }

    // 监控客户端发送控制指令的序列化函数
    std::vector<uint8_t> serializeSetVelocityPacket(uint32_t sequence, const SetVelocityData &data)
    {
        std::vector<uint8_t> payload;

        appendUint32(payload, static_cast<uint32_t>(data.robot_id));

        appendDouble(payload, data.linear_velocity);

        appendDouble(payload, data.angular_velocity);

        return serializePacket(
            MSG_SET_VELOCITY,
            sequence,
            payload);
    }

    // 当机器人不在线的时候将不在线发送到监控客户端中 序列化函数
    std::vector<uint8_t> serializeCommandResultPacket(uint32_t sequence, const CommandResultData &data)
    {
        std::vector<uint8_t> payload;

        appendUint32(payload, static_cast<uint32_t>(data.robot_id));
        appendUint32(payload, data.command_sequence);
        appendUint16(payload, data.result);

        return serializePacket(
            MSG_COMMAND_RESULT,
            sequence,
            payload);
    }
    std::vector<uint8_t> serializeRobotStatusPacket( uint32_t sequence,const RobotStatusData &data)
    {
        std::vector<uint8_t> payload;

        appendUint32(payload, static_cast<uint32_t>(data.robot_id));
        appendUint16(payload,data.status);

        return serializePacket(
            MSG_ROBOT_STATUS,
            sequence,
            payload);
    }
    std::vector<uint8_t> serializeStateStreamRequestPacket(uint32_t sequence,const StateStreamRequestData& data)
    {
        std::vector<uint8_t> payload;

        appendUint32(payload,static_cast<uint32_t>(data.robot_id));
        appendUint32(payload,data.duration_ms);

        return serializePacket(
            MSG_STATE_STREAM_REQUEST,
            sequence,
            payload);
    } 

    std::vector<uint8_t> serializeHeartbeatPacket(uint32_t sequence,const HeartbeatData& data)
    {
        std::vector<uint8_t> payload;
        appendUint32(payload,static_cast<uint32_t>(data.robot_id));

        return serializePacket(MSG_HEARTBEAT,sequence,payload);
    }
    /**
     * 服务端接收到数据将数据解析
     * 将buffer解析成packet中的Header和payload
     */
    ParseResult tryParsePacket(
        const std::vector<uint8_t>& buffer,
        Packet& packet,
        std::size_t& consumed)  // 解析的buffer收到的所有数据 将解析后的真实数据不是协议放到packet(id 位姿 速度 电量 时间) consumed 收到的所有数据的长度
        {
            consumed = 0;
            /**
             * 1. 连Header都没有完整
             */
            if (buffer.size() < PACKET_HEADER_SIZE)
            {
                return PARSE_NEED_MORE;
            }

            /**
             * 解析Header
             */
            std::size_t offset = 0;
            uint16_t magic = readUint16(buffer, offset);
            offset += sizeof(uint16_t);

            uint16_t type = readUint16(buffer, offset);
            offset += sizeof(uint16_t);

            uint32_t length = readUint32(buffer, offset);
            offset += sizeof(uint32_t);

            uint32_t sequence = readUint32(buffer, offset);
            offset += sizeof(uint32_t);

            /**
             * 检查协议是不是合理
             */
            if (magic != PACKET_MAGIC)
            {
                return PARSE_ERROR;
            }

            if (length > MAX_PAYLOAD_SIZE)
            {
                return PARSE_ERROR;
            }

            /**
             * 判断完整的Packet有没有收完
             */
            std::size_t total_size =
                PACKET_HEADER_SIZE + length;

            if (buffer.size() < total_size)
            {
                /**
                 * Header 收到了
                 * 但是payload还没有收完整
                 * 这就是半包
                 */
                return PARSE_NEED_MORE;
            }

            /**
             * 填充Packer
             */
            packet.header.magic = magic;
            packet.header.type = type;
            packet.header.length = length;
            packet.header.sequence = sequence;

            packet.payload.assign(
                buffer.begin() + PACKET_HEADER_SIZE,
                buffer.begin() + total_size);

            consumed = total_size;

            return PARSE_OK;
        }

    /**
     * 解析ROS发送的数据
     * 解析真实的数据(不包括协议头)解析packet这个结构体，
     * 将解析的结果保存到state中
     * 将packet中的payload解析成具体的数值
     * state就是具体的数值
     */
    bool payloadToRobotState(const Packet& packet, RobotStateData& state)
    {
        if (packet.header.type != MSG_ROBOT_STATE)
        {
            return false;
        }
        constexpr std::size_t ROBOT_STATE_PAYLOAD_SIZE = 4 + 8 * 6 + 4 + 4;

        if (packet.payload.size() != ROBOT_STATE_PAYLOAD_SIZE)
        {
            return false;
        }
        std::size_t offset = 0;
        state.robot_id = static_cast<int32_t>(readUint32(packet.payload,offset));
        offset += sizeof(uint32_t);

        state.x = readDouble(packet.payload,offset);
        offset += sizeof(double);

        state.y = readDouble(packet.payload,offset);
        offset += sizeof(double);

        state.yaw = readDouble(packet.payload,offset);
        offset += sizeof(double);

        state.linear_velocity = readDouble(packet.payload,offset);
        offset += sizeof(double);

        state.angular_velocity = readDouble(packet.payload,offset);
        offset += sizeof(double);

        state.battery = readDouble(packet.payload,offset);
        offset += sizeof(double);

        state.timestamp_sec = readUint32(packet.payload,offset);
        offset += sizeof(uint32_t);

        state.timestamp_nsec = readUint32(packet.payload,offset);

        return true;
    }
    // 将REGISTER Packet中的payload解析成RegisterData
    bool payloadToRegister(const Packet &packet, RegisterData &data)
    {
        // 先判断是不是注册消息
        if (packet.header.type != MSG_REGISTER )
        {
            return false;
        }
        // role 2字节 + robot_id 4字节 = 6字节
        constexpr std::size_t REGISTER_PAYLOAD_SIZE = sizeof(uint16_t) + sizeof(uint32_t);

        if (packet.payload.size() != REGISTER_PAYLOAD_SIZE)
        {
            return false;
        }

        std::size_t offset = 0;
        // 读取role
        data.role = static_cast<ClientRole>(readUint16(packet.payload,offset));
        offset += sizeof(uint16_t);
        // 读取robot_id
        data.robot_id = static_cast<int32_t>(readUint32(packet.payload,offset));
        return true;
    }
    bool payloadToRobotStatus(const Packet &packet,RobotStatusData &data)
    {
        if (packet.header.type != MSG_ROBOT_STATUS)
        {
            return false;
        }

        constexpr std::size_t PAYLOAD_SIZE =
            sizeof(uint32_t) +
            sizeof(uint16_t);

        if (packet.payload.size() != PAYLOAD_SIZE)
        {
            return false;
        }

        std::size_t offset = 0;

        data.robot_id =static_cast<int32_t>( readUint32(packet.payload,offset));
        offset += sizeof(uint32_t);
        data.status =readUint16( packet.payload,offset);

        return true;
    }

    // 监控客户端发送控制指令的反序列化函数
    bool payloadToSetVelocity(const Packet &packet,SetVelocityData &data)
    {
        if (packet.header.type != MSG_SET_VELOCITY)
        {
            return false;
        }

        // robot_id 4字节
        // linear_velocity 8字节
        // angular_velocity 8字节
        constexpr std::size_t SET_VELOCITY_PAYLOAD_SIZE =
            sizeof(uint32_t) +
            sizeof(double) +
            sizeof(double);

        if (packet.payload.size() !=
            SET_VELOCITY_PAYLOAD_SIZE)
        {
            return false;
        }

        std::size_t offset = 0;
        data.robot_id = static_cast<int32_t>(readUint32(packet.payload,offset));
        offset += sizeof(uint32_t);
        data.linear_velocity = static_cast<double>(readDouble(packet.payload,offset));
        offset += sizeof(double);
        data.angular_velocity = static_cast<double>(readDouble(packet.payload,offset));
        return true;
    }
    // 接收中心客户端发送的机器人不在线的消息 反序列化函数
    bool payloadToCommandResult(const Packet &packet,CommandResultData &data)
    {
        if (packet.header.type != MSG_COMMAND_RESULT)
        {
            return false;
        }

        constexpr std::size_t PAYLOAD_SIZE =
            sizeof(uint32_t) +
            sizeof(uint32_t) +
            sizeof(uint16_t);

        if (packet.payload.size() != PAYLOAD_SIZE)
        {
            return false;
        }

        std::size_t offset = 0;

        data.robot_id = static_cast<int32_t>( readUint32(packet.payload, offset));
        offset += sizeof(uint32_t);
        data.command_sequence = readUint32( packet.payload, offset);
        offset += sizeof(uint32_t);
        data.result = readUint16(packet.payload,offset);

        return true;
    }
    bool payloadToStateStreamRequest(const Packet& packet,StateStreamRequestData& data)
    {
        if (packet.header.type != MSG_STATE_STREAM_REQUEST)
        {
            return false;
        }

        constexpr std::size_t PAYLOAD_SIZE = sizeof(uint32_t) + sizeof(uint32_t);

        if (packet.payload.size() != PAYLOAD_SIZE)
        {
            return false;
        }
        std::size_t offset = 0;
        data.robot_id = static_cast<int32_t>(readUint32(packet.payload,offset));
        offset += sizeof(uint32_t);
        data.duration_ms = readUint32(packet.payload,offset);
        return true;
        
    }

    bool payloadToHeartbeat(const Packet& packet,HeartbeatData& data)
    {
        if (packet.header.type != MSG_HEARTBEAT)
        {
            return false;
        }
        constexpr std::size_t PAYLOAD_SIZE = sizeof(uint32_t);

        if (packet.payload.size() != PAYLOAD_SIZE)
        {
            return false;
        }
        data.robot_id = static_cast<int32_t>(readUint32(packet.payload,0));
        return true;
    }

    std::string payloadToString(const Packet& packet)
    {
        return std::string(
            packet.payload.begin(),
            packet.payload.end()
        );
    }
    
}