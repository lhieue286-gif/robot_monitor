#include "robot_monitor/gateway_tcp_server.h"


// 中心服务器

int main(int argc, char const *argv[])
{
    robot_monitor::GatewayTcpServer tcp_server;
    if (!tcp_server.start("127.0.0.1",8080)) // 不在同一台机器时，要改成：tcp_server.start("0.0.0.0", 8080);
    {
        return -1;
    }
    while (true)
    {
        tcp_server.pollOnce(10);//时间怎么定
    }
    
    
    return 0;
}
