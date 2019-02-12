// SUBSCRIBER NODE ("isam2")

// ROS/PACKAGE INCLUDES
/* ************************************************************************* */

#include "ros/ros.h"
#include "std_msgs/String.h"

#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>

#include <legged_vio/CameraMeasurement.h>
#include <sensor_msgs/Imu.h>

// ISAM2 INCLUDES
/* ************************************************************************* */

// Camera observations of landmarks (i.e. pixel coordinates) will be stored as Point2 (x, y).
#include <gtsam/geometry/Point2.h>

// Each variable in the system (poses and landmarks) must be identified with a unique key.
// We can either use simple integer keys (1, 2, 3, ...) or symbols (X1, X2, L1).
// Here we will use Symbols
#include <gtsam/inference/Symbol.h>

// We want to use iSAM2 to solve the structure-from-motion problem incrementally, so
// include iSAM2 here
#include <gtsam/nonlinear/ISAM2.h>

// iSAM2 requires as input a set of new factors to be added stored in a factor graph,
// and initial guesses for any new variables used in the added factors
#include <gtsam/nonlinear/NonlinearFactorGraph.h>
#include <gtsam/nonlinear/Values.h>

// In GTSAM, measurement functions are represented as 'factors'. Several common factors
// have been provided with the library for solving robotics/SLAM/Bundle Adjustment problems.
// Here we will use Projection factors to model the camera's landmark observations.
// Also, we will initialize the robot at some location using a Prior factor.
#include <gtsam/slam/PriorFactor.h>
#include <gtsam/slam/ProjectionFactor.h>

// ADDITIONAL INCLUDES
/* ************************************************************************* */

#include <vector>
#include <memory>

using namespace std;
using namespace message_filters;
using namespace legged_vio;
using namespace sensor_msgs;
using namespace gtsam;


// CALLBACK WRAPPER CLASS
/* ************************************************************************* */

class Callbacks { 

private:
  
  // Hold node handle initialized in main
  shared_ptr<ros::NodeHandle> nh_ptr;
  
  // Create a Factor Graph and Values to hold the new data, accessible from callback function
  NonlinearFactorGraph graph;
  Values initialEstimate;  

  // --> Create iSAM2 object
  unique_ptr<ISAM2> isam;

  // Camera calibration intrinsic matrix
  Cal3_S2::shared_ptr K;

  // Camera calibration extrinsic
  double Tx; // distance from cam0 to cam1

  // --> Camera observation noise model (has to do with IMU?)
  noiseModel::Isotropic::shared_ptr measurementNoise = noiseModel::Isotropic::Sigma(2, 1.0); // one pixel in u and v

public:
 
  Callbacks(shared_ptr<ros::NodeHandle> nh_ptr_copy) : nh_ptr(move(nh_ptr_copy)) {

    // Initialize camera calibration matrix using YAML file
    vector<int> cam0_intrinsics(4);
    vector<int> cam1_intrinsics(4);
    // YAML intrinsics (pinhole): [fu fv pu pv]
    nh_ptr->getParam("cam0/intrinsics", cam0_intrinsics); 
    nh_ptr->getParam("cam1/intrinsics", cam1_intrinsics);
    // GTSAM Cal3_S2 (doubles): (fx, fy, s, u0, v0)
    Cal3_S2::shared_ptr K(new Cal3_S2(cam0_intrinsics[0], cam0_intrinsics[1], 0.0, 
                                      cam0_intrinsics[2], cam0_intrinsics[3])); 
		// Stereo Camera Extrinsic (distance between 2 cameras)
		vector<double> T_cam1(16);
		nh_ptr->getParam("cam1/T_cn_cnm1", T_cam1);
		Tx = T_cam1[3];

		ROS_INFO("cam0/intrinsics exists? %d", nh_ptr->hasParam("cam0/intrinsics")); 
		ROS_INFO("intrinsics: %d, %d, %d, %d", cam0_intrinsics[0], cam0_intrinsics[1], cam0_intrinsics[2], cam0_intrinsics[3]);
    ROS_INFO("Tx: %f, %f, %f, %f", T_cam1[0], T_cam1[1], T_cam1[2], T_cam1[3]);
    
    // iSAM2 performs partial relinearization/variable reordering at each step A parameter
    // parameter structure allows setting of properties: relinearization threshold & type of linear solver
    // this example: set the relinearization threshold small so the iSAM2 result will approach the batch result.
    ISAM2Params parameters;
    parameters.relinearizeThreshold = 0.01;
    parameters.relinearizeSkip = 1;
    isam.reset(new ISAM2(parameters));
  }

  void callback(const CameraMeasurementConstPtr& camera_msg, const ImuConstPtr& imu_msg) {
  
    vector<FeatureMeasurement> feature_vector = camera_msg->features;  
   
    ROS_INFO("%lu total features", feature_vector.size());
		ROS_INFO("Tx: %f", Tx);

    for (int i = 0; i < feature_vector.size(); i++) {

			//double d = feature_vector[i].u1 - feature_vector[i].u0; // distance from cam0 to cam1 in pixels
      //double denom = -d / Tx; 
			//X = 

			Point2 measurement(feature_vector[i].u0, feature_vector[i].v0);

			// graph.emplace_shared<GenericProjectionFactor<Pose3, Point3, Cal3_S2> >(measurement, measurementNoise, Symbol('x', i), Symbol('l', j), K);
				
		}

    // Use features u, v image coordinates to estimate feature X, Y, Z locations in camera frame
    // maybe with openCV, maybe something already in msckf_vio  

    // Create Point3 objects (feature in camera -> feature in world) 

    // Create Pose3 object (estimated camera pose)  

    // Add to Factor Graph

    // Optimize using ISAM

    // Print Results to ROS_INFO
  }

// potential ROS messages
//ROS_INFO("IMU header: [%s]", imu_msg->header.frame_id);
//std_msgs::Header header = camera_msg->header;
//geometry_msgs::Quaternion ori = imu_msg->orientation;
//ROS_INFO("Camera Coor: [id = %d, u = %f, v = %f]", features[0].id, features[0].u0, features[0].v0);


};

// MAIN
/* ************************************************************************* */
int main(int argc, char **argv) {

  ros::init(argc, argv, "isam2"); // specify name of node and ROS arguments
  shared_ptr<ros::NodeHandle> nh_ptr = make_shared<ros::NodeHandle>();

  // Instantiate class containing callbacks and necessary variables
  Callbacks callbacks_obj(nh_ptr);

  // Subscribe to "features" and "imu" topics simultaneously
  message_filters::Subscriber<CameraMeasurement> feature_sub(*nh_ptr, "/minitaur/image_processor/features", 1);
  message_filters::Subscriber<Imu> imu_sub(*nh_ptr, "/imu0", 1);
  TimeSynchronizer<CameraMeasurement, Imu> sync(feature_sub, imu_sub, 10);
  sync.registerCallback(boost::bind(&Callbacks::callback, &callbacks_obj, _1, _2));

  // Loop, pumping all callbacks (specified in subscriber object)
  ros::spin(); 

  return 0;
}

