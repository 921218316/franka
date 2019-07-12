// =============================================================================
// Name        : spacenav.h
// Author      : Hélio Ochoa
// Description : Allows user to control End-Effector(EE) pose in EE and Base(O)
//               frame with space-mouse.
// =============================================================================
#ifndef SPACENAV_H
#define SPACENAV_H

#include "ros/ros.h"
#include <sstream>
#include <Eigen/Dense>
#include <sensor_msgs/Joy.h>
#include <geometry_msgs/PoseStamped.h>
#include <fstream>
#include <SFML/Window/Keyboard.hpp>
#include <math.h>

#include <franka_msgs/FrankaState.h>

#define PI  3.14159265358979323846  /* pi */
#define CLEANWINDOW "\e[2J\e[H"

namespace franka_udrilling {

  class Spacenav{
    public:
      Spacenav(ros::NodeHandle &nh);
      ~Spacenav();

      // VARIABLES
      Eigen::Matrix<double, 6, 1> spacenav_motion;

      Eigen::Vector3d position_d;  // end-effector desired position
      Eigen::Quaterniond orientation_d;  // end-effector desired orientation

      Eigen::Matrix4d O_T_EE; // Transformation matrix referred to robot
      Eigen::Matrix4d T_spacenav;  //  Transformation matrix referred to spacenav
      Eigen::Matrix4d Tx, Ty, Tz;

      std::string link_name;

      int spacenav_button_1 = 0;
      int spacenav_button_2 = 0;

      ros::Publisher pose_pub; // Franka Pose publisher

      Eigen::Matrix<double, 6, 1> O_F_ext_hat_K;
      Eigen::Matrix<double, 6, 1> K_F_ext_hat_K;

      // FUNCS
      void MotionControl(Eigen::Matrix4d& Xd); // Function to execute motion control
      void createPublishersAndSubscribers(); // Function to create publishers and subscribers
      void posePublisherCallback(geometry_msgs::PoseStamped& marker_pose, Eigen::Vector3d& position, Eigen::Quaterniond& quaternion);
      Eigen::Vector3d polynomial3_trajectory(Eigen::Vector3d& pi, Eigen::Vector3d& pf, double ti, double tf, double t);
      Eigen::VectorXd robot_pose(Eigen::Matrix4d& Xd);  // Function to get the current robot pose

    private:
      // VARIABLES
      ros::NodeHandle nh_;  // private node NodeHandle
      ros::Subscriber spacenav_sub; // Spacenav Subscriber
      ros::Subscriber franka_state_sub; // Franka State Subscriber
      ros::Time time_lapse, last_time; // Time variables to change modes with spacenav

      int flag_mode = 0;  // Flag to control robot in base frame or end-effector frame
      bool flag_motion = false; // Flag to determine if there is spacenav motion
      int franka_state_flag = 0;

      // FUNCS
      void joyCallback(const sensor_msgs::Joy::ConstPtr& msg);
      void frankaStateCallback(const franka_msgs::FrankaState::ConstPtr& msg);

  };

} // namespace franka_udrilling


#endif // SPACENAV_H
