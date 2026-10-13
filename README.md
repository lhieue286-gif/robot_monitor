# robot_monitor

基于 ROS 1 和 C++ 的多机器人状态监控示例，包含机器人模拟器、TCP 网关和监控客户端。

## 编译

需要 ROS 1、catkin、`roscpp`、`std_msgs` 和 `geometry_msgs`。将此包放在 catkin 工作空间的 `src/` 下，然后在工作空间根目录执行：

```bash
catkin_make
source devel/setup.bash
```

## 运行

先启动 ROS master，再分别在三个终端中运行以下命令。每个终端都需先执行 `source devel/setup.bash`。

```bash
# 终端 1：中心网关
rosrun robot_monitor robot_gateway

# 终端 2：两个模拟机器人及其 TCP 客户端
roslaunch robot_monitor multi_robot.launch

# 终端 3：监控客户端
rosrun robot_monitor robot_state_client
```

默认 TCP 地址为 `127.0.0.1:8080`。多机器人启动配置在 `launch/multi_robot.launch` 中。
