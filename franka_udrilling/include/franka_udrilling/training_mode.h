// =============================================================================
// Name        : training_mode.h
// Author      : Hélio Ochoa
// Description : Switches to torque controller and allows the user to train the
//               robot.
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <fstream>

#include <controller_interface/multi_interface_controller.h>
#include <hardware_interface/joint_command_interface.h>
#include <hardware_interface/robot_hw.h>
#include <ros/node_handle.h>
#include <ros/time.h>
#include <Eigen/Dense>
#include <sensor_msgs/Joy.h>

#include <franka_hw/franka_model_interface.h>
#include <franka_hw/franka_state_interface.h>


#define CLEANWINDOW "\e[2J\e[H"

namespace franka_udrilling {

class TrainingMode : public controller_interface::MultiInterfaceController<franka_hw::FrankaModelInterface,
                                                                            hardware_interface::EffortJointInterface,
                                                                            franka_hw::FrankaStateInterface> {

  public:
    TrainingMode();
    ~TrainingMode();

    bool init(hardware_interface::RobotHW* robot_hw, ros::NodeHandle& node_handle) override;
    void update(const ros::Time&, const ros::Duration& period) override;

  private:

    // Saturation
    Eigen::Matrix<double, 7, 1> saturateTorqueRate(
        const Eigen::Matrix<double, 7, 1>& tau_d_calculated,
        const Eigen::Matrix<double, 7, 1>& tau_J_d);  // NOLINT (readability-identifier-naming)

    std::unique_ptr<franka_hw::FrankaStateHandle> state_handle_;
    std::unique_ptr<franka_hw::FrankaModelHandle> model_handle_;
    std::vector<hardware_interface::JointHandle> joint_handles_;

    const double delta_tau_max_{1.0};

    int count; // file counter
    std::ofstream file;
    std::ofstream desired_o;
    std::ofstream points;
    int flag_mode = 0;
    int flag_print = 0;

    // spacenav
    ros::Subscriber spacenav_sub;
    ros::Time time_lapse, last_time; // Time variables to change modes with spacenav
    int spacenav_button_1 = 0;
    int spacenav_button_2 = 0;
    void joyCallback(const sensor_msgs::Joy::ConstPtr& msg);

};

}  // namespace franka_udrilling
