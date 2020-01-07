// =============================================================================
// Name        : cartesian_impedance_controller.h
// Author      : Hélio Ochoa
// Description : Cartesian impedance controller where the orientation control
//               is based on rotation error (just for Gazebo)
// =============================================================================
#include <sstream>
#include <fstream>
#include <cmath>

#include <Eigen/Dense>

#include <controller_interface/multi_interface_controller.h>
#include <controller_interface/controller_base.h>
#include <hardware_interface/joint_command_interface.h>
#include <hardware_interface/robot_hw.h>
#include <pluginlib/class_list_macros.h>

#include <kdl_parser/kdl_parser.hpp>
#include <kdl/chainfksolverpos_recursive.hpp>
#include <kdl/chainjnttojacsolver.hpp>
#include <kdl/chaindynparam.hpp>

#include "ros/ros.h"
#include <sensor_msgs/Joy.h>
#include <geometry_msgs/PoseStamped.h>
#include <dynamic_reconfigure/server.h>
#include <tf/transform_broadcaster.h>

#include <franka_simulation/compliance_paramConfig.h>

#define PI  3.14159265358979323846  /* pi */
#define CLEANWINDOW "\e[2J\e[H"

using namespace KDL;

namespace franka_simulation{

class CartesianImpedanceController : public controller_interface::MultiInterfaceController<
                                            hardware_interface::EffortJointInterface>{

    public:
        CartesianImpedanceController();
        ~CartesianImpedanceController();

        bool init(hardware_interface::RobotHW* robot_hw, ros::NodeHandle& node_handle) override;
        void starting(const ros::Time&) override;
        void update(const ros::Time&, const ros::Duration& period) override;

    private:
        // control variables -------------------------------------------------------
        std::vector<hardware_interface::JointHandle> joint_handles_;

        double filter_params_{0.01};
        Eigen::Matrix<double, 7, 1> q, dq, effort;  //joints position, joints velocity and joints effort (force or torque)
        Eigen::Matrix<double, 7, 1> q_start;
        Eigen::Matrix4d O_T_EE, O_T_EE_d;
        Eigen::Matrix<double, 6, 7> J;  // jacobian
        Eigen::Matrix<double, 7, 7> M;  // inertia or mass
        Eigen::Matrix<double, 7, 1> C;  // coriolis
        Eigen::Matrix<double, 7, 1> g;  // gravity
        Eigen::Matrix<double, 6, 6> cartesian_stiffness_;
        Eigen::Matrix<double, 6, 6> cartesian_damping_;
        Eigen::Matrix3d Kp_d_, Dp_d_; // position stiffness and damping in desired frame
        Eigen::Matrix3d Kp_d_target_, Dp_d_target_;
        Eigen::Matrix3d Ko_d_, Do_d_; // orientation stiffness and damping in desired frame
        Eigen::Matrix3d Ko_d_target_, Do_d_target_;
        Eigen::Matrix<double, 7, 7> nullspace_stiffness_;
        Eigen::Matrix<double, 7, 7> nullspace_stiffness_target_;
        Eigen::Vector3d position_d_;
        Eigen::Vector3d position_d_target_;
        Eigen::Quaterniond orientation_d_;
        Eigen::Quaterniond orientation_d_target_;
        Eigen::Matrix3d R_d_;
        Eigen::Matrix<double, 6, 1> velocity_d_;
        Eigen::Matrix<double, 6, 1> error, velocity_error;

        // file with gains and file for tracking -----------------------------------
        int count; // file counter
        std::ofstream file_tracking;
        std::ifstream file_gains;

        // important functions -----------------------------------------------------
        Eigen::Vector3d R2r(Eigen::Matrix3d& Rotation); // Convert a rotation matrix in a rotation vector
        double wrapToPI(double& angle); // Wrap between [-PI, PI]
        double wrapTo2PI(double& angle); // Wrap between [0, 2*PI]

        // Equilibrium pose subscriber 
        ros::Subscriber sub_equilibrium_pose_;
        void equilibriumPoseCallback(const geometry_msgs::PoseStampedConstPtr& msg);
        
        // pose publisher
        ros::Publisher poseEE_pub;
        void posePublisherCallback(ros::Publisher& pose_pub, Eigen::Vector3d& position, Eigen::Quaterniond& orientation);

        // Dynamic reconfigure
        std::unique_ptr<dynamic_reconfigure::Server<franka_simulation::compliance_paramConfig>> dynamic_server_compliance_param_;
        ros::NodeHandle dynamic_reconfigure_compliance_param_node_;
        void complianceParamCallback(franka_simulation::compliance_paramConfig& config, uint32_t level);

        // KDL variables -----------------------------------------------------------
        std::string root_name, end_effector_name;
        KDL::Chain kdl_chain;
        KDL::Tree kdl_tree;
        KDL::Frame forward_kinematics;
        KDL::Jacobian jac;
        KDL::JntSpaceInertiaMatrix inertia; // Inertia output matrix
        KDL::JntArray coriolis; // Coriolis output matrix
        KDL::JntArray gravity;  // Gravity output matrix
        Vector g_vector = {0.0, 0.0, -9.80665};   // Gravity vector
        int n_joints; // number of joints

        // KDL functions -----------------------------------------------------------
        bool FK(KDL::Frame& kdl_frame, Eigen::Matrix<double, 7, 1>& q_values); // forward kinematics
        bool jacobian(KDL::Jacobian& kdl_jacobian, Eigen::Matrix<double, 7, 1> q_values);  // Jacobian
        bool dynamic(KDL::JntSpaceInertiaMatrix& kdl_inertia, KDL::JntArray& kdl_coriolis, KDL::JntArray& kdl_gravity, Eigen::Matrix<double, 7, 1>& q_values, Eigen::Matrix<double, 7, 1>& dq_values, Vector& g_vector); // Dynamic

        // posture optimization ----------------------------------------------------
        Eigen::Matrix<double, 7, 1> maxJointLimits;
        Eigen::Matrix<double, 7, 1> minJointLimits;
        Eigen::Matrix<double, 7, 1> gradient;  // gradient mechanical joint limit
        double derivative_computation( const double q_i, const double maxJointLimit_i, const double minJointLimit_i);
        template<int N> // number of joints or DOF
        void gradient_mechanical_joint_limit( Eigen::Matrix<double, N, 1>& gradient_mechanical_joint_limit_out, const Eigen::Matrix<double, N, 1> q, const Eigen::Matrix<double, N, 1> maxJointLimits, const Eigen::Matrix<double, N, 1> minJointLimits );

};

} //namespace franka_simulation
