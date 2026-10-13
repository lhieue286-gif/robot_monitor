#ifndef ROBOT_MONITOR_ROBOT_MODEL_H
#define ROBOT_MONITOR_ROBOT_MODEL_H

// 机器人端的机器人运动代码,类中的成员函数可以直接使用这个类中的成员变量，不需要再把成员变量当参数传进去。
namespace robot_monitor
{
class RobotModel
{
public:
    RobotModel(); // 无参构造函数
    // RobotModel(int robot_id, double battery);有参构造
    ~RobotModel(); //析构函数

    //// 配置机器人基本参数。
    void configure(int robot_id,
                   double max_linear_speed,
                   double max_angular_speed,
                   double initial_battery);
    //根据dt更新位置、朝向和电量
    void update(double dt);

    //当外面给一个数据来修改当前的数据 设置最大速度,同时限制在最大速度范围内。
    void setVelocity(double linear_velocity, double angular_velocity);
    // 恢复到原来 reset 服务的默认状态。
    void reset();

    /**
     * 因为这些变量是 private，类外面不能直接这样访问：
     * double current_x = model.x();
     * 只是读取数据。
     */
    int robotId() const;
    double x() const;
    double y() const;
    double yaw() const;
    double linearVelocity() const;
    double angularVelocity() const;
    double battery() const;

private:

    int robot_id_;

    double x_;
    double y_;
    double yaw_;

    double linear_velocity_;
    double angular_velocity_;
    double battery_;

    double max_linear_speed_;
    double max_angular_speed_;


};
}
#endif 