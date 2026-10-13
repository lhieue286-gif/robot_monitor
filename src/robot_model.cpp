#include "robot_monitor/robot_model.h"

#include <algorithm>
#include <cmath>

namespace robot_monitor
{
    /**
     * 构造函数初始化列表,无参构造 RobotModel()括号里面没有参数就是无参构造
     * RobotModel::RobotModel(int robot_id, double battery): robot_id_(robot_id),battery_(battery){}这个是有参构造函数初始化列表
     */
    RobotModel::RobotModel()
        : robot_id_(1),
          x_(0),
          y_(0),
          yaw_(0.0),
          linear_velocity_(0.50),
          angular_velocity_(0.10),
          battery_(100.0),
          max_linear_speed_(1.0),
          max_angular_speed_(0.8)
    {
    }
    // 配置机器人基本参数。
    void RobotModel::configure(int robot_id,
                   double max_linear_speed,
                   double max_angular_speed,
                   double initial_battery)
    {
        robot_id_ = robot_id;
        max_linear_speed_ = max_linear_speed;
        max_angular_speed_ = max_angular_speed;
        battery_ = initial_battery;

        // 保证当前初始速度也不会超过配置的最大速度。
        setVelocity(linear_velocity_, angular_velocity_);
    }
    void RobotModel::update(double dt)
    {
        x_ += linear_velocity_ * std::cos(yaw_) * dt;
        y_ += linear_velocity_ * std::sin(yaw_) * dt;
        yaw_ += angular_velocity_ * dt;

        const double pi = 3.14159265358979323846;
        if (yaw_ > pi)
        {
            yaw_ -= 2.0 * pi;
        }
        else if (yaw_ < -pi)
        {
            yaw_ += 2.0 * pi;
        }

        battery_ -= 0.01 * dt;
        if (battery_ < 0.0)
        {
            battery_ = 0.0;
        }
    }
    void RobotModel::setVelocity(double linear_velocity, double angular_velocity)
    {
        linear_velocity_ = std::max(-max_linear_speed_,
                                    std::min(linear_velocity, max_linear_speed_));

        angular_velocity_ = std::max(-max_angular_speed_,
                                     std::min(angular_velocity, max_angular_speed_));
    }

    void RobotModel::reset()
    {
        x_ = 0.0;
        y_ = 0.0;
        yaw_ = 0.0;
        linear_velocity_ = 0.0;
        angular_velocity_ = 0.0;
        battery_ = 100.0;
    }

    int RobotModel::robotId() const
    {
        return robot_id_;
    }

    double RobotModel::x() const
    {
        return x_;
    }

    double RobotModel::y() const
    {
        return y_;
    }

    double RobotModel::yaw() const
    {
        return yaw_;
    }

    double RobotModel::linearVelocity() const
    {
        return linear_velocity_;
    }

    double RobotModel::angularVelocity() const
    {
        return angular_velocity_;
    }

    double RobotModel::battery() const
    {
        return battery_;
    }

    RobotModel::~RobotModel() // 析构函数
    {
    }

} // namespace robot_monitor


// 机器人端的机器人运动代码