// =============================================================================
// Name        : universal_udrilling_v2_node.cpp
// Author      : Hélio Ochoa
// Description : For all kind of drills (0.6, 0.5, 0.4 mm) and robot performs 
//               the drilling in any end-effector direction or orientation... 
//               (without station)             
// =============================================================================
#include <franka_udrilling/udrilling_state.h>
#include <tf/transform_broadcaster.h>
#include <visualization_msgs/Marker.h>


// =============================================================================
//                              STATE MACHINE 
// =============================================================================
#define MOVE2POINT 0
#define PREDRILL 1
#define DRILL 2
#define DRILLUP 3
#define DRILLDOWN 4
#define NEXTPOINT 5
#define INTERRUPT 6
// =============================================================================

int main(int argc, char **argv){

    ros::init(argc, argv, "universal_udrilling_v2_node");
    ros::NodeHandle nh;
    franka_udrilling::uDrillingState panda(nh);

    // =============================================================================
    //                               tf broadcaster
    tf::TransformBroadcaster pandaEEd_br, mould_br;
    tf::Transform pandaEEd_tf, mould_tf;
    

    // =============================================================================
    //                            VIZUALIZATION MARKERS
    ros::Publisher marker_pub = nh.advertise<visualization_msgs::Marker>("visualization_marker", 20);
    visualization_msgs::Marker points;
    points.header.frame_id = "/panda_link0";
    points.header.stamp = ros::Time::now();
    points.ns = "points";
    points.id = 0;
    points.action = visualization_msgs::Marker::ADD;
    points.type = visualization_msgs::Marker::POINTS;
    points.pose.orientation.w = 1.0;

    // POINTS markers use x and y scale for width/height respectively
    points.scale.x = 0.01;
    points.scale.y = 0.01;

    // Set the color -- be sure to set alpha to something non-zero!
    points.color.a = 1.0;
    points.color.r = 1.0;
    points.color.g = 0.0;
    points.color.b = 0.0;


    // =============================================================================
    //                   GET THE DESIRED ROTATION FROM FILE
    std::ifstream orientation_file;
    orientation_file.open("/home/panda/catkin_ws/src/TOOLING4G/franka_udrilling/co_manipulation_data/mould_orientation");

    double qx, qy, qz, qw;
    if(orientation_file.is_open()){
        orientation_file >> qx >> qy >> qz >> qw;
    }
    else{
        std::cout << "Error opening the orientation file!" << std::endl;
        return(0);
    }
    orientation_file.close();
    Eigen::Quaterniond Qd;  // desired Quaternion
    Qd.vec()[0] = qx;
    Qd.vec()[1] = qy;
    Qd.vec()[2] = qz;
    Qd.w() = qw;
    // std::cout << Qd.coeffs() << std::endl;

    Eigen::Matrix3d Rd(Qd); // desired Rotation
    // std::cout << Rd << std::endl;


    // =============================================================================
    //                        GET MOULD POINTS FROM FILE
    Eigen::MatrixXd P;  // matrix to save the mould points
    std::ifstream points_file;
    points_file.open("/home/panda/catkin_ws/src/TOOLING4G/franka_udrilling/co_manipulation_data/mould_points");
    // points_file.open("/home/panda/catkin_ws/src/TOOLING4G/franka_udrilling/co_manipulation_data/mould_line_points");
    double X, Y, Z;
    int n_points = 0;
    P.resize(3, n_points + 1);
    if(points_file.is_open()){
        while(points_file >> X >> Y >> Z){
        // save the values in the matrix
        P.conservativeResize(3, n_points + 1);
        P(0, n_points) = X;
        P(1, n_points) = Y;
        P(2, n_points) = Z;
        n_points++;
        }
    }
    else{
        std::cout << "\nError opening the mould points file!" << std::endl;
        return(0);
    }
    points_file.close();
    // std::cout << P.transpose() << std::endl;

    geometry_msgs::Point p_points;
    int i = 0;
    while(i < n_points){
        p_points.x = P(0, i);
        p_points.y = P(1, i);
        p_points.z = P(2, i);
        points.points.push_back(p_points);
        i++;
    }


    // =============================================================================
    //                            GET INITIAL POSE
    Eigen::Matrix4d O_T_EE;
    Eigen::VectorXd pose(7,1);
    O_T_EE = panda.O_T_EE;
    pose = panda.robotPose(O_T_EE);
    

    // =============================================================================
    //                        TRAJECTORY TO FIRST POINT
    // position
    Eigen::Vector3d pi, pf;
    pi << pose[0], pose[1], pose[2];
    pf << P(0, 0), P(1, 0), pi(2);
    double ti = 2.0;
    double tf = 5.0;
    double t = 0.0;
    double delta_t = 0.001;

    // orientation
    Eigen::Quaterniond oi, of;
    oi.coeffs() << pose[3], pose[4], pose[5], pose[6];
    of.coeffs() << oi.coeffs();
    double t1 = 0.0;
    double delta_t1 = delta_t/(tf-ti);
    
    // move up 
    Eigen::Vector3d delta_up;
    delta_up << 0.0, 0.0, 0.25;


    // =============================================================================
    //                     DRILLING TRAJECTORY CONDITIONS
    Eigen::Vector3d delta_drill, delta_roof, delta_predrill, delta_point, delta_goal, delta_limit;;
    delta_drill << 0.0, 0.0, 0.001;
    delta_roof << 0.0, 0.0, 0.002;  
    
    delta_predrill << 0.0, 0.0, 0.005; 
    delta_point << 0.0, 0.0, 0.003;

    delta_goal << 0.0, 0.0, 0.012; 
    delta_limit << 0.0, 0.0, 0.016;
    
    Eigen::Vector3d p_roof, p_goal, p_limit;
    p_roof.setZero();
    p_goal.setZero();
    p_limit.setZero();
    

    // =============================================================================
    //                           FORCE LIMIT CONDITIONS
    double Fz_max = 12.0;
    // double Fz_min = 4.0;


    // =============================================================================
    //                              SELECT DRILL LOOP
    int select_drill = 0;
    int select_drill_print = 0;
    int systemRet = 0;
    ros::Rate loop_rate(1000);
    while( (ros::ok()) && (select_drill == 0) ){

        if(select_drill_print == 0){
            std::cout << CLEANWINDOW << "Please select the drill: <6>(0.6mm) | <5>(0.5mm) | <4>(0.4mm)" << std::endl;
            select_drill_print = 1;
        }

        if( sf::Keyboard::isKeyPressed(sf::Keyboard::Num6) || sf::Keyboard::isKeyPressed(sf::Keyboard::Numpad6) ){

            if(select_drill_print == 1){
                std::cout << CLEANWINDOW << "Drill selected: <6>(0.6mm)!!!" << std::endl;
                select_drill_print = 2;
            }
            
            // change compliance parameters
            systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Kpz 1700.0");
            systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Dpz 80.0");
            if(systemRet == -1){
                std::cout << CLEANWINDOW << "The system method failed!" << std::endl;
            }
            select_drill = 1;

            break;
        }
        else if( sf::Keyboard::isKeyPressed(sf::Keyboard::Num5) || sf::Keyboard::isKeyPressed(sf::Keyboard::Numpad5) ){

            if(select_drill_print == 1){
                std::cout << CLEANWINDOW << "Drill selected: <5>(0.5mm)!!!" << std::endl;
                select_drill_print = 2;
            }
            
            // change compliance parameters
            systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Kpz 1500.0");
            systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Dpz 80.0");
            if(systemRet == -1){
                std::cout << CLEANWINDOW << "The system method failed!" << std::endl;
            }
            select_drill = 1;

            break;
        }
        else if( sf::Keyboard::isKeyPressed(sf::Keyboard::Num4) || sf::Keyboard::isKeyPressed(sf::Keyboard::Numpad4) ){

            if(select_drill_print == 1){
                std::cout << CLEANWINDOW << "Drill selected: <4>(0.4mm)!!!" << std::endl;
                select_drill_print = 2;
            }
            
            // change compliance parameters
            systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Kpz 1300.0");
            systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Dpz 70.0");
            if(systemRet == -1){
                std::cout << CLEANWINDOW << "The system method failed!" << std::endl;
            }
            Fz_max = 10.0;
            select_drill = 1;

            break;
        }
     
        ros::spinOnce();
        loop_rate.sleep();
    }


    // =============================================================================
    //                                  MAIN LOOP
    // =============================================================================
    Eigen::Vector3d position_d(pi);
    Eigen::Quaterniond orientation_d(oi);

    int flag_drilling = 0;
    int flag_print = 0;
    int flag_interrupt_print = 0;
    int flag_move2point = 0;
    int n_points_done = 0;
    double result = 0.0;
    
    while(ros::ok()){

        switch (flag_drilling) { ////////////////////////////////////////////////////

            // ======================================================================
            case MOVE2POINT:
                if(flag_print == 0){
                    std::cout << CLEANWINDOW << "Robot is moving to a mold point..." << std::endl;
                    flag_print = 1;
                }

                // << MOVE2POINT >>
                if(flag_move2point == 0){
                    if( (t >= ti) && (t <= tf) ){
                        position_d = panda.polynomial3Trajectory(pi, pf, ti, tf, t);
                    }
                    else if(t > tf){
                        flag_move2point = 1;
                        pi << position_d;
                        pf << P(0, n_points_done), P(1, n_points_done), P(2, n_points_done);
                        pf << pf - Rd*delta_point;
                        t = 0;  // reset time
                        oi.coeffs() << orientation_d.coeffs();
                        of.coeffs() << Qd.vec()[0], Qd.vec()[1], Qd.vec()[2], Qd.w();
                        t1 = 0.0; // reset orientation time

                        // change compliance parameters
                        systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Ipx 0.5");
                        systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Ipy 0.5");
                        if(systemRet == -1){
                            std::cout << CLEANWINDOW << "The system method failed!" << std::endl;
                        }
                    }
                }
                // << DOWN >>
                else if(flag_move2point == 1){
                    ti = 0.0;
                    tf = 4.0;
                    if( (t >= ti) && (t <= tf) ){
                        position_d = panda.polynomial3Trajectory(pi, pf, ti, tf, t);
                        if ( t1 <= 1.0 ){
                            orientation_d = oi.slerp(t1, of);
                            orientation_d.normalize();
                        }
                        t1 = t1 + delta_t1;
                    }
                    else if(t > tf){
                        flag_drilling = PREDRILL;
                        pi << position_d;
                        pf << P(0, n_points_done), P(1, n_points_done), P(2, n_points_done);
                        pf << pf + Rd*delta_predrill;
                        t = 0;  // reset time

                        // change compliance parameters
                        systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Ipx 0.0");
                        systemRet = system("rosrun dynamic_reconfigure dynparam set /dynamic_reconfigure_compliance_param_node Ipy 0.0");
                        if(systemRet == -1){
                            std::cout << CLEANWINDOW << "The system method failed!" << std::endl;
                        }
                                          
                    }
                }
                t = t + delta_t;

                // INTERRUPT
                if(panda.spacenav_button_2 == 1){
                    flag_drilling = INTERRUPT;
                }

                break;

            // ======================================================================
            case PREDRILL:     
                if(flag_print == 1){
                    std::cout << CLEANWINDOW << "Robot is pre-drilling..." << std::endl;
                    flag_print = 2;
                }

                // << PRE DRILL >>
                ti = 0.0;
                tf = 10.0;
                if( (t >= ti) && (t <= tf) ){
                    position_d = panda.polynomial3Trajectory(pi, pf, ti, tf, t);
                }
                else if(t > tf){
                    flag_drilling = DRILL;
                    pi << position_d;
                    pf << pi + Rd*delta_drill;
                    p_roof << P(0, n_points_done), P(1, n_points_done), P(2, n_points_done);
                    p_roof << p_roof + Rd*delta_roof;
                    p_goal << P(0, n_points_done), P(1, n_points_done), P(2, n_points_done);
                    p_goal << p_goal + Rd*delta_goal;
                    p_limit << P(0, n_points_done), P(1, n_points_done), P(2, n_points_done);
                    p_limit << p_limit + Rd*delta_limit;
                    t = 0;  // reset time
                }
                t = t + delta_t;

                // INTERRUPT
                if(panda.spacenav_button_2 == 1){
                    flag_drilling = INTERRUPT;
                }

                break;

            // ======================================================================
            case DRILL:
                if(flag_print == 2){
                    std::cout << CLEANWINDOW << "HOLE Nº" << n_points_done << " | ROBOT IS DRILLING, IF YOU WOULD LIKE TO STOP PRESS SPACENAV BUTTON <2>! | Fz = " << panda.K_F_ext_hat_K[2] << std::endl;
                    flag_print = 3;
                }

                ///////////////////////////////////////////////
                //                 Force Limit 
                if( panda.K_F_ext_hat_K[2] > Fz_max ){
                    pf << pi;
                }
                ///////////////////////////////////////////////
                O_T_EE = panda.O_T_EE;
                pose = panda.robotPose(O_T_EE);  // get current pose
                result = pose(2) - p_goal(2);
                if( result > 0.0 ){
                // if( panda.K_F_ext_hat_K[2] > Fz_min ){
                    // << DRILL >>
                    ti = 0.0;
                    tf = 0.6;
                    if( (t >= ti) && (t <= tf) ){
                        position_d = panda.polynomial3Trajectory(pi, pf, ti, tf, t);
                    }
                    else if(t > tf){
                        flag_drilling = DRILLUP;
                        pi << position_d;
                        pf << p_roof;
                        t = 0;  // reset time
                    }
                    t = t + delta_t;
                }
                else{
                    flag_drilling = NEXTPOINT;
                    O_T_EE = panda.O_T_EE;
                    pose = panda.robotPose(O_T_EE);  // get current pose
                    pi << pose[0], pose[1], pose[2];
                    delta_up << 0.0, 0.0, 0.15;
                    pf << pi - Rd*delta_up;
                    t = 0;
                }

                // INTERRUPT
                if(panda.spacenav_button_2 == 1){
                    flag_drilling = INTERRUPT;
                }

                break;

            // ======================================================================
            case DRILLUP:
                // << DRILLUP >> 
                ti = 0.0;
                tf = 0.5;
                if( (t >= ti) && (t <= tf) ){
                    position_d = panda.polynomial3Trajectory(pi, pf, ti, tf, t);
                }
                else if(t > tf){
                    flag_drilling = DRILLDOWN;
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

            // ======================================================================
            case DRILLDOWN:
                // << DRILLDOWN >> 
                ti = 1.0;
                tf = 2.0;
                if( (t >= ti) && (t <= tf) ){
                    position_d = panda.polynomial3Trajectory(pi, pf, ti, tf, t);
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

            // ======================================================================
            case NEXTPOINT:
                if(flag_print == 3){
                    std::cout << CLEANWINDOW << "The hole is complete and the robot is moving up! | Fz(N): " << panda.K_F_ext_hat_K[2] << std::endl;
                    flag_print = 0;
                }

                // << UP >> 
                ti = 0.0;
                tf = 4.0;
                if( (t >= ti) && (t <= tf) ){
                    position_d = panda.polynomial3Trajectory(pi, pf, ti, tf, t);
                }
                else if(t > tf){
                    if(n_points_done < n_points-1){
                        n_points_done++;  // next point
                    }
                    else{
                        if(flag_print == 0){
                            std::cout << CLEANWINDOW << "ALL HOLES COMPLETE!" << std::endl;
                            flag_print = 5;
                        }
                        return 0;
                    }

                    flag_drilling = MOVE2POINT;
                    flag_move2point = 0;
                    pi << position_d;
                    pf << P(0, n_points_done), P(1, n_points_done), pi(2);;
                    delta_up << 0.0, 0.0, 0.25;
                    ti = 0.0;
                    tf = 4.0;
                    delta_t1 = delta_t/(tf-ti);
                    t = 0;  // reset time                   
                }
                t = t + delta_t;

                // INTERRUPT
                if(panda.spacenav_button_2 == 1){
                flag_drilling = INTERRUPT;
                }

                break;

            // ======================================================================
            case INTERRUPT:
                if(flag_interrupt_print == 0){
                    std::cout << CLEANWINDOW << "PROGRAM INTERRUPTED! If you would like to continue please press spacenav button <1>..." << std::endl;
                    flag_interrupt_print = 1;
                }

                if(panda.spacenav_button_1 == 1){
                    flag_drilling = NEXTPOINT;
                    flag_interrupt_print = 0;
                    flag_print = 3;
                    O_T_EE = panda.O_T_EE;
                    pose = panda.robotPose(O_T_EE);  // get current pose
                    pi << pose[0], pose[1], pose[2];
                    delta_up << 0.0, 0.0, 0.15;
                    pf << pi - Rd*delta_up;
                    t = 0;
                }

                break;

        } ///////////////////////////////////////////////////////////////////////////
        
             
        // std::cout << CLEANWINDOW << position_d << std::endl;
        // std::cout << CLEANWINDOW << orientation_d.coeffs() << std::endl;
        panda.posePublisherCallback(position_d, orientation_d);

        // ===========================================================================
        //                     TF AND VISUALIZATION MARKERS

        // Draw the panda EE desired transform
        pandaEEd_tf.setOrigin( tf::Vector3(position_d(0), position_d(1), position_d(2)) );
        pandaEEd_tf.setRotation( tf::Quaternion(orientation_d.vec()[0], orientation_d.vec()[1], orientation_d.vec()[2], orientation_d.w()) );
        pandaEEd_br.sendTransform(tf::StampedTransform(pandaEEd_tf, ros::Time::now(), "/panda_link0", "/panda_EE_d"));

        // Draw the mould transform
        mould_tf.setOrigin( tf::Vector3(P(0, n_points_done), P(1, n_points_done), P(2, n_points_done)) );
        mould_tf.setRotation( tf::Quaternion(Qd.vec()[0], Qd.vec()[1], Qd.vec()[2], Qd.w()) );
        mould_br.sendTransform(tf::StampedTransform(mould_tf, ros::Time::now(), "/panda_link0", "/mold"));

        // Draw the points
        marker_pub.publish(points);
           
        // ============================================================================

        if(sf::Keyboard::isKeyPressed(sf::Keyboard::Space))
            break;

        ros::spinOnce();
        loop_rate.sleep();
    }

    return 0;
}
