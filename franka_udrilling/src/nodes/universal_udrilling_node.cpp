// =============================================================================
// Name        : universal_udrilling_node.cpp
// Author      : Hélio Ochoa
// Description :
// =============================================================================
#include <franka_udrilling/spacenav.h>
#include <tf/transform_broadcaster.h>
#include <visualization_msgs/Marker.h>

using namespace franka_udrilling;

geometry_msgs::PoseStamped marker_pose;

// STATE MACHINE ---------------------------------------------------------------
#define MOVE2POINT 0
#define MOVEDOWN 1
#define DRILL 2
#define ROOFUP 3
#define ROOFDOWN 4
#define MOVEUP 5
#define INTERRUPT 6
// -----------------------------------------------------------------------------

int main(int argc, char **argv){

  ros::init(argc, argv, "universal_udrilling_node");
  ros::NodeHandle nh;
  franka_udrilling::Spacenav panda(nh);


  // ---------------------------------------------------------------------------
  // MOULD POINTS
  // ---------------------------------------------------------------------------
  Eigen::MatrixXd POINTS;  // matrix to save the mould points
  std::ifstream file_points;
  std::string path_points;
  path_points = "/home/helio/kst/udrilling/mould/points";
  file_points.open(path_points);

  double X, Y, Z;
  double delta_z = 0.002;
  int points_column = 0;
  POINTS.resize(3, points_column + 1);
  if(file_points.is_open()){
    while(file_points >> X >> Y >> Z){
      // save the values in the matrix
      POINTS.conservativeResize(3, points_column + 1);
      POINTS(0, points_column) = X;
      POINTS(1, points_column) = Y;
      POINTS(2, points_column) = Z - delta_z;
      points_column++;
    }
  }
  else{
    std::cout << "\nError open the file!" << std::endl;
  }
  file_points.close();
  // std::cout << POINTS.transpose() << std::endl;

  // Markers
  ros::Publisher marker_pub = nh.advertise<visualization_msgs::Marker>("visualization_marker", 20);

  visualization_msgs::Marker points, line_strip;
  points.header.frame_id = line_strip.header.frame_id = "/panda_link0";
  points.header.stamp = line_strip.header.stamp = ros::Time::now();
  points.ns = "points";
  line_strip.ns = "lines";
  points.id = 0;
  line_strip.id = 1;
  points.action = line_strip.action = visualization_msgs::Marker::ADD;
  points.type = visualization_msgs::Marker::POINTS;
  line_strip.type = visualization_msgs::Marker::LINE_STRIP;
  points.pose.orientation.w = line_strip.pose.orientation.w = 1.0;

  // POINTS markers use x and y scale for width/height respectively
  points.scale.x = 0.01;
  points.scale.y = 0.01;
  points.scale.z = 0.01;

  // Set the color -- be sure to set alpha to something non-zero!
  points.color.a = 1.0;
  points.color.r = 1.0;
  points.color.g = 0.0;
  points.color.b = 0.0;

  // LINE_STRIP/LINE_LIST markers use only the x component of scale, for the line width
  line_strip.scale.x = 0.01;

  // Line strip is green
  line_strip.color.a = 1.0;
  line_strip.color.g = 1.0;

  geometry_msgs::Point p, l;  // points and lines
  int i = 0;
  while(i < points_column){
    p.x = POINTS(0, i);
    p.y = POINTS(1, i);
    p.z = POINTS(2, i);
    points.points.push_back(p);
    i++;
  }


  // ---------------------------------------------------------------------------
  // GET THE DESIRED ROTATION
  // ---------------------------------------------------------------------------
  std::ifstream file_desired_o;
  std::string path_desired_o;
  path_desired_o = "/home/helio/kst/udrilling/mould/desired_o";
  file_desired_o.open(path_desired_o);

  double qx, qy, qz, qw;
  if(file_desired_o.is_open()){
    file_desired_o >> qx >> qy >> qz >> qw;
  }
  else{
    std::cout << "Error open the file!" << std::endl;
  }
  file_desired_o.close();
  Eigen::Quaterniond Qd;  // desired Quaternion
  Qd.vec()[0] = qx;
  Qd.vec()[1] = qy;
  Qd.vec()[2] = qz;
  Qd.w() = qw;
  // std::cout << Qd.coeffs() << std::endl;

  Eigen::Matrix3d Rd(Qd); // desired Rotation
  // std::cout << Rd << std::endl;


  // ---------------------------------------------------------------------------
  // tf broadcaster
  // ---------------------------------------------------------------------------
  tf::TransformBroadcaster br, br_d;
  tf::Transform transform, transform_d;


  // ---------------------------------------------------------------------------
  // GET INITIAL POSE
  // ---------------------------------------------------------------------------
  Eigen::Matrix4d O_T_EE_i;
  Eigen::VectorXd pose_i(7,1);
  O_T_EE_i = panda.O_T_EE;
  pose_i = panda.robot_pose(O_T_EE_i);


  // ---------------------------------------------------------------------------
  // TRAJECTORY TO FIRST POINT
  // ---------------------------------------------------------------------------
  // position
  Eigen::Vector3d pi, pf;
  int num_cols = points_column-1;
  pi << pose_i[0], pose_i[1], pose_i[2];
  pf << POINTS(0, num_cols), POINTS(1, num_cols), pi[2];
  double ti = 2.0;
  double tf = 5.0;
  double t = 0.0;
  double delta_t = 0.001;

  // orientation
  Eigen::Quaterniond oi, of;
  oi.coeffs() << pose_i[3], pose_i[4], pose_i[5], pose_i[6];
  of.coeffs() << Qd.vec()[0], Qd.vec()[1], Qd.vec()[2], Qd.w();
  double t1 = 0.0;
  double delta_t1 = delta_t/(tf-ti);

  // ---------------------------------------------------------------------------
  // DRILLING TRAJECTORY CONDITIONS
  // ---------------------------------------------------------------------------
  Eigen::Vector3d delta_drill, delta_roof, delta_goal, delta_limit;
  delta_drill << 0.0, 0.0, 0.001;
  delta_roof << 0.0, 0.0, 0.001;
  delta_goal << 0.0, 0.0, 0.012;
  delta_limit << 0.0, 0.0, 0.015;
  Eigen::Vector3d p_roof, p_goal, p_limit;
  p_roof.setZero();
  p_goal.setZero();
  p_limit.setZero();

  double limit_z = 0.000001;

  // ---------------------------------------------------------------------------
  // TRAJECTORY UP CONDITIONS
  // ---------------------------------------------------------------------------
  Eigen::Vector3d delta_up;
  delta_up << 0.0, 0.0, 0.1;

  // ---------------------------------------------------------------------------
  // MAIN LOOP
  // ---------------------------------------------------------------------------
  Eigen::Vector3d position_d(pi);
  Eigen::Quaterniond orientation_d(oi);

  int flag_drilling = 0;
  int flag_print = 0;
  int flag_interrupt = 0;
  double result = 0.0;

  ros::Rate loop_rate(1000);
  int count = 0;
  while(ros::ok()){

    switch (flag_drilling) { ///////////////////////////////////////////////////

      // -----------------------------------------------------------------------
      case MOVE2POINT:
        if(flag_print == 0){
          std::cout << CLEANWINDOW << "ROBOT IS MOVING TO A MOULD POINT..." << std::endl;
          flag_print = 1;
        }

        // --> MOVE TO MOULD POINT <--
        if( (t >= ti) && (t <= tf) ){
          position_d = panda.polynomial3_trajectory(pi, pf, ti, tf, t);
          if ( t1 <= 1.0 ){
            orientation_d = oi.slerp(t1, of);
            orientation_d.normalize();
          }
          t1 = t1 + delta_t1;
        }
        else if(t > tf){
          flag_drilling = MOVEDOWN;
          O_T_EE_i = panda.O_T_EE;
          pose_i = panda.robot_pose(O_T_EE_i);  // get current pose
          pi << pose_i[0], pose_i[1], pose_i[2];
          pf << POINTS(0, num_cols), POINTS(1, num_cols), POINTS(2, num_cols);
          t = 0;  // reset time
        }
        t = t + delta_t;

        // INTERRUPT
        if(panda.spacenav_button_2 == 1){
          flag_drilling = INTERRUPT;
        }

        break;

      // -----------------------------------------------------------------------
      case MOVEDOWN:
        if(flag_print == 1){
          std::cout << CLEANWINDOW << "ROBOT IS MOVING DOWN!" << std::endl;
          flag_print = 2;
        }

        // --> MOVE DOWN <--
        ti = 0.0;
        tf = 5.0;
        if( (t >= ti) && (t <= tf) ){
          position_d = panda.polynomial3_trajectory(pi, pf, ti, tf, t);
        }
        else if(t > tf){
          if(flag_print == 2){
            std::cout << CLEANWINDOW << "ROBOT IS READY, PLEASE PRESS BUTTON <1> OF SPACENAV TO START DRILLING!" << std::endl;
            flag_print = 3;
          }
          if(panda.spacenav_button_1 == 1){
            flag_drilling = DRILL;
            pi << POINTS(0, num_cols), POINTS(1, num_cols), POINTS(2, num_cols);  // define new initial position
            pf << pi + Rd*delta_drill;
            p_roof << pi + Rd*delta_roof;
            p_goal << pi + Rd*delta_goal;
            p_limit << pi + Rd*delta_limit;
            t = 0;  // reset time
          }
        }
        t = t + delta_t;

        // INTERRUPT
        if(panda.spacenav_button_2 == 1){
          flag_drilling = INTERRUPT;
        }

        break;

      // -----------------------------------------------------------------------
      case DRILL:
        // if(flag_print == 3){
        //   std::cout << CLEANWINDOW << "ROBOT IS DRILLING, IF YOU WOULD LIKE TO STOP PRESS SPACENAV BUTTON <2>..."  << std::endl;
        //   flag_print = 4;
        // }

        std::cout << CLEANWINDOW << "ROBOT IS DRILLING, IF YOU WOULD LIKE TO STOP PRESS SPACENAV BUTTON <2>! |" << " z limit: " << p_limit(2) << std::endl;

        O_T_EE_i = panda.O_T_EE;
        pose_i = panda.robot_pose(O_T_EE_i);  // get current pose
        result = pose_i(2) - p_goal(2);
        if( result > 0.0 ){
          // --> DRILL <--
          ti = 0.0;
          tf = 1.5;
          if( (t >= ti) && (t <= tf) ){
            position_d = panda.polynomial3_trajectory(pi, pf, ti, tf, t);
          }
          else if(t > tf){
            flag_drilling = ROOFUP;
            pi << position_d;
            pf << p_roof;
            t = 0;  // reset time
          }
          t = t + delta_t;
        }
        else{
          flag_drilling = MOVEUP;
          flag_print = 4;
          O_T_EE_i = panda.O_T_EE;
          pose_i = panda.robot_pose(O_T_EE_i);  // get current pose
          pi << pose_i[0], pose_i[1], pose_i[2];
          pf << pi - Rd*delta_up;
          t = 0;
        }

        // INTERRUPT
        if(panda.spacenav_button_2 == 1){
          flag_drilling = INTERRUPT;
        }

        break;

      // -----------------------------------------------------------------------
      case ROOFUP:

        std::cout << CLEANWINDOW << "ROBOT IS DRILLING, IF YOU WOULD LIKE TO STOP PRESS SPACENAV BUTTON <2>! |" << " z limit: " << p_limit(2) << std::endl;

        // --> UP <--
        ti = 0.0;
        tf = 0.8;
        if( (t >= ti) && (t <= tf) ){
          position_d = panda.polynomial3_trajectory(pi, pf, ti, tf, t);
        }
        else if(t > tf){
          flag_drilling = ROOFDOWN;
          pf << pi;
          pi << p_roof;
          t = 0;  // reset time
        }
        t = t + delta_t;

        // INTERRUPT
        if(panda.spacenav_button_2 == 1){
          flag_drilling = INTERRUPT;
        }

        break;

      // -----------------------------------------------------------------------
      case ROOFDOWN:

        std::cout << CLEANWINDOW << "ROBOT IS DRILLING, IF YOU WOULD LIKE TO STOP PRESS SPACENAV BUTTON <2>! |" << " z limit: " << p_limit(2) << std::endl;

        // --> DOWN <--
        ti = 0.0;
        tf = 0.8;
        if( (t >= ti) && (t <= tf) ){
          position_d = panda.polynomial3_trajectory(pi, pf, ti, tf, t);
        }
        else if(t > tf){
          flag_drilling = DRILL;
          pi << position_d;
          if( pi(2) < p_limit(2) ){
            pf << pi;
          }
          else{
            pf << pi + Rd*delta_drill;
          }
          t = 0;  // reset time
        }
        t = t + delta_t;

        // INTERRUPT
        if(panda.spacenav_button_2 == 1){
          flag_drilling = INTERRUPT;
        }

        break;

      // -----------------------------------------------------------------------
      case MOVEUP:
        if(flag_print == 4){
          std::cout << CLEANWINDOW << "THE HOLE IS COMPLETE AND THE ROBOT IS MOVING UP!" << " result(m): " << result << std::endl;
          flag_print = 0;
        }

        // --> MOVE UP <--
        ti = 0.0;
        tf = 3.0;
        if( (t >= ti) && (t <= tf) ){
          position_d = panda.polynomial3_trajectory(pi, pf, ti, tf, t);
        }
        else if(t > tf){

          if(num_cols > 0){
          num_cols--;
          }
          else{
            if(flag_print == 0){
              std::cout << CLEANWINDOW << "ALL HOLES COMPLETE!" << std::endl;
              flag_print = 5;
            }
            return 0;
          }

          flag_drilling = MOVE2POINT;
          O_T_EE_i = panda.O_T_EE;
          pose_i = panda.robot_pose(O_T_EE_i);  // get current pose
          pi << pose_i[0], pose_i[1], pose_i[2];
          pf << POINTS(0, num_cols), POINTS(1, num_cols), pi[2];
          ti = 0.0;
          tf = 3.0;
          t = 0;  // reset time
        }
        t = t + delta_t;

        // INTERRUPT
        if(panda.spacenav_button_2 == 1){
          flag_drilling = INTERRUPT;
        }

        break;

      // -----------------------------------------------------------------------
      case INTERRUPT:

        if(flag_interrupt == 0){
          std::cout << CLEANWINDOW << "PROGRAM INTERRUPTED! IF YOU WOULD LIKE TO CONTINUE, PLEASE PRESS SPACENAV BUTTON <1>..." << std::endl;
          flag_interrupt = 1;
        }

        if(panda.spacenav_button_1 == 1){
          flag_drilling = MOVEUP;
          flag_interrupt = 0;
          flag_print = 4;
          O_T_EE_i = panda.O_T_EE;
          pose_i = panda.robot_pose(O_T_EE_i);  // get current pose
          pi << pose_i[0], pose_i[1], pose_i[2];
          pf << pi - Rd*delta_up;
          t = 0;
        }

        break;

    } //////////////////////////////////////////////////////////////////////////

    //Define Z limit
    if(panda.spacenav_motion(2) > 0.0){
      p_limit(2) += limit_z;
    }
    else if(panda.spacenav_motion(2) < 0.0){
      p_limit(2) -= limit_z;
    }

    // std::cout << CLEANWINDOW << position_d << std::endl;
    // std::cout << CLEANWINDOW << orientation_d.coeffs() << std::endl;
    panda.posePublisherCallback(marker_pose, position_d, orientation_d);

    // -------------------------------------------------------------------------
    // TF AND VISUALIZATION MARKERS
    // -------------------------------------------------------------------------
    // Draw the mould transform
    transform.setOrigin( tf::Vector3(POINTS(0, 0), POINTS(1, 0), POINTS(2, 0)) );
    transform.setRotation( tf::Quaternion(Qd.vec()[0], Qd.vec()[1], Qd.vec()[2], Qd.w()) );
    br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "/panda_link0", "/mould"));

    // Draw the desired EE transform
    transform_d.setOrigin( tf::Vector3(position_d(0), position_d(1), position_d(2)) );
    transform_d.setRotation( tf::Quaternion(orientation_d.vec()[0], orientation_d.vec()[1], orientation_d.vec()[2], orientation_d.w()) );
    br_d.sendTransform(tf::StampedTransform(transform_d, ros::Time::now(), "/panda_link0", "/panda_EE_d"));

    // Draw the points of the mould work space
    marker_pub.publish(points);
    // -------------------------------------------------------------------------

    if(sf::Keyboard::isKeyPressed(sf::Keyboard::Space))
      break;

    ros::spinOnce();
    loop_rate.sleep();
    count++;
  }

  return 0;
}
