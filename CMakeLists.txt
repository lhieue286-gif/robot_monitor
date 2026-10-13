cmake_minimum_required(VERSION 3.0.2)
project(robot_monitor)

## Compile as C++11, supported in ROS Kinetic and newer
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## Find catkin macros and libraries
## if COMPONENTS list like find_package(catkin REQUIRED COMPONENTS xyz)
## is used, also find other catkin packages
find_package(catkin REQUIRED COMPONENTS
  geometry_msgs
  message_generation
  roscpp
  std_msgs
)

## System dependencies are found with CMake's conventions
# find_package(Boost REQUIRED COMPONENTS system)


## Uncomment this if the package has a setup.py. This macro ensures
## modules and global scripts declared therein get installed
## See http://ros.org/doc/api/catkin/html/user_guide/setup_dot_py.html
# catkin_python_setup()

################################################
## Declare ROS messages, services and actions ##
################################################

## To declare and build messages, services or actions from within this
## package, follow these steps:
## * Let MSG_DEP_SET be the set of packages whose message types you use in
##   your messages/services/actions (e.g. std_msgs, actionlib_msgs, ...).
## * In the file package.xml:
##   * add a build_depend tag for "message_generation"
##   * add a build_depend and a exec_depend tag for each package in MSG_DEP_SET
##   * If MSG_DEP_SET isn't empty the following dependency has been pulled in
##     but can be declared for certainty nonetheless:
##     * add a exec_depend tag for "message_runtime"
## * In this file (CMakeLists.txt):
##   * add "message_generation" and every package in MSG_DEP_SET to
##     find_package(catkin REQUIRED COMPONENTS ...)
##   * add "message_runtime" and every package in MSG_DEP_SET to
##     catkin_package(CATKIN_DEPENDS ...)
##   * uncomment the add_*_files sections below as needed
##     and list every .msg/.srv/.action file to be processed
##   * uncomment the generate_messages entry below
##   * add every package in MSG_DEP_SET to generate_messages(DEPENDENCIES ...)

## Generate messages in the 'msg' folder
add_message_files(
  FILES
  RobotState.msg
) # 自己定义的消息 RobotState.msg  --> ROS message_generation --> RobotState.h

## Generate services in the 'srv' folder
add_service_files(
  FILES
  ResetRobot.srv
) #自己定义的服务消息 ResetRobot.srv --> ResetRobot.h

## Generate actions in the 'action' folder
# add_action_files(
#   FILES
#   Action1.action
#   Action2.action
# )

## Generate added messages and services with any dependencies listed here
generate_messages(
  DEPENDENCIES
  geometry_msgs
  std_msgs
) # 告诉catkin根据刚才声明的 .msg 和 .srv 生成对应的 C++ 代码。

################################################
## Declare ROS dynamic reconfigure parameters ##
################################################

## To declare and build dynamic reconfigure parameters within this
## package, follow these steps:
## * In the file package.xml:
##   * add a build_depend and a exec_depend tag for "dynamic_reconfigure"
## * In this file (CMakeLists.txt):
##   * add "dynamic_reconfigure" to
##     find_package(catkin REQUIRED COMPONENTS ...)
##   * uncomment the "generate_dynamic_reconfigure_options" section below
##     and list every .cfg file to be processed

## Generate dynamic reconfigure parameters in the 'cfg' folder
# generate_dynamic_reconfigure_options(
#   cfg/DynReconf1.cfg
#   cfg/DynReconf2.cfg
# )

###################################
## catkin specific configuration ##
###################################
## The catkin_package macro generates cmake config files for your package
## Declare things to be passed to dependent projects
## INCLUDE_DIRS: uncomment this if your package contains header files
## LIBRARIES: libraries you create in this project that dependent projects also need
## CATKIN_DEPENDS: catkin_packages dependent projects also need
## DEPENDS: system dependencies of this project that dependent projects also need
catkin_package(
  INCLUDE_DIRS include # 我的包对外提供头文件，这些头文件放在 include/ 目录里。

  LIBRARIES 
  robot_protocol # 我的 ROS 包对外提供一个库，名字叫 robot_protocol。
  robot_simulator

  CATKIN_DEPENDS
  geometry_msgs
  message_runtime
  roscpp
  std_msgs
) #这个理解一下

###########
## Build ##
###########

## Specify additional locations of header files
## Your package locations should be listed before other locations
include_directories(
  include
  ${catkin_INCLUDE_DIRS}
) # 编译当前工程的 cpp 时：先去：robot_monitor/include/ 再去: catkin/ROS提供的头文件路径 寻找 #include 的文件

## Declare a C++ library
# add_library(${PROJECT_NAME}
#   src/${PROJECT_NAME}/robot_monitor.cpp
# )
add_library(
    robot_protocol
    src/protocol.cpp
)

add_library(
    robot_simulator
    src/robot_simulator.cpp
    src/robot_model.cpp
)

add_library(
    gateway_tcp_server
    src/gateway_tcp_server.cpp
)

target_link_libraries(
    gateway_tcp_server
    robot_protocol
    ${catkin_LIBRARIES}
)

add_library(
    robot_tcp_client
    src/robot_tcp_client.cpp
)

target_link_libraries(
    robot_tcp_client
    robot_protocol
    ${catkin_LIBRARIES}
)

# add_library(
#     ros_bridge
#     src/ros_bridge.cpp
#     src/gateway_tcp_server.cpp
#     src/protocol.cpp
# )

# add_library(
#     gateway_tcp_server
#     src/gateway_tcp_server.cpp
#     src/protocol.cpp
# )

## Add cmake target dependencies of the library
## as an example, code may need to be generated before libraries
## either from message generation or dynamic reconfigure
# add_dependencies(robot_simulator ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})

## Declare a C++ executable
## With catkin_make all packages are built within a single CMake context
## The recommended prefix ensures that target names across packages don't collide
add_executable(robot_simulator_node src/robot_simulator_node.cpp)
# add_executable(tcp_server  src/tcp_server.cpp)
# add_executable(tcp_client src/tcp_client.cpp)
add_executable(robot_gateway src/robot_gateway.cpp)
add_executable(robot_state_client src/robot_state_client.cpp)
add_executable(robot_tcp_client_node src/robot_tcp_client_node.cpp)
# robot_gateway.cpp

## Rename C++ executable without prefix
## The above recommended prefix causes long target names, the following renames the
## target back to the shorter version for ease of user use
## e.g. "rosrun someones_pkg node" instead of "rosrun someones_pkg someones_pkg_node"
# set_target_properties(${PROJECT_NAME}_node PROPERTIES OUTPUT_NAME node PREFIX "")

## Add cmake target dependencies of the executable
## same as for the library above
# 规定构建顺序。add_dependencies(A B) 先构建 B--> B完成 --> 再允许构建 A
add_dependencies(robot_simulator_node ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS}) # 理解一下
add_dependencies(robot_gateway ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})
add_dependencies(robot_simulator ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})
add_dependencies(
    robot_tcp_client
    ${${PROJECT_NAME}_EXPORTED_TARGETS}
    ${catkin_EXPORTED_TARGETS}
)

add_dependencies(
    robot_tcp_client_node
    ${${PROJECT_NAME}_EXPORTED_TARGETS}
    ${catkin_EXPORTED_TARGETS}
)

## Specify libraries to link a library or executable target against
target_link_libraries(
  robot_simulator_node
  robot_simulator
  ${catkin_LIBRARIES}
) # catkin_LIBRARIES 链接ros依赖的文件

# target_link_libraries(
#     tcp_server
#     robot_protocol
#     ${catkin_LIBRARIES}
# )

# target_link_libraries(
#     tcp_client
#     robot_protocol
#     ${catkin_LIBRARIES}
# )

target_link_libraries(
    robot_gateway
    robot_protocol
    gateway_tcp_server
    ${catkin_LIBRARIES}
)
target_link_libraries(
    robot_state_client
    robot_protocol
    ${catkin_LIBRARIES}
)

target_link_libraries(
    robot_tcp_client_node
    robot_tcp_client
    robot_protocol
    ${catkin_LIBRARIES}
)

#############
## Install ##
#############

# all install targets should use catkin DESTINATION variables
# See http://ros.org/doc/api/catkin/html/adv_user_guide/variables.html

## Mark executable scripts (Python etc.) for installation
## in contrast to setup.py, you can choose the destination
# catkin_install_python(PROGRAMS
#   scripts/my_python_script
#   DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
# )

## Mark executables for installation
## See http://docs.ros.org/melodic/api/catkin/html/howto/format1/building_executables.html
# install(TARGETS ${PROJECT_NAME}_node
#   RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
# )

## Mark libraries for installation
## See http://docs.ros.org/melodic/api/catkin/html/howto/format1/building_libraries.html
# install(TARGETS ${PROJECT_NAME}
#   ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
#   LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
#   RUNTIME DESTINATION ${CATKIN_GLOBAL_BIN_DESTINATION}
# )

## Mark cpp header files for installation
# install(DIRECTORY include/${PROJECT_NAME}/
#   DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
#   FILES_MATCHING PATTERN "*.h"
#   PATTERN ".svn" EXCLUDE
# )

## Mark other files for installation (e.g. launch and bag files, etc.)
# install(FILES
#   # myfile1
#   # myfile2
#   DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
# )

#############
## Testing ##
#############

## Add gtest based cpp test target and link libraries
# catkin_add_gtest(${PROJECT_NAME}-test test/test_robot_monitor.cpp)
# if(TARGET ${PROJECT_NAME}-test)
#   target_link_libraries(${PROJECT_NAME}-test ${PROJECT_NAME})
# endif()

## Add folders to be run by python nosetests
# catkin_add_nosetests(test)
