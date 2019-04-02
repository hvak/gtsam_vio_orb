// SUBSCRIBER NODE ("isam2")

// ROS/PACKAGE INCLUDES
/* ************************************************************************* */

#include "ros/ros.h"
#include "std_msgs/String.h"

#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>

#include <legged_vio/CameraMeasurement.h>
#include <sensor_msgs/Imu.h>

#include <sensor_msgs/PointCloud2.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/common/centroid.h>

// ISAM2 INCLUDES
/* ************************************************************************* */

#include <gtsam/geometry/Pose3.h>
#include <gtsam/geometry/Cal3_S2Stereo.h>

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
#include <gtsam/slam/PriorFactor.h>
#include <gtsam/slam/StereoFactor.h>

// ADDITIONAL INCLUDES
/* ************************************************************************* */

#include <vector>
#include <memory>
#include <map>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace message_filters;
using namespace legged_vio;
using namespace sensor_msgs;
using namespace gtsam;

// PARAMETERS TO SPECIFY FOR OTHER NODES
/* ************************************************************************* */

struct LaunchVariables {
  string feature_topic_id = "minitaur/image_processor/features";
  string imu_topic_id = "/zed/imu/data_raw"; // "/zed/imu/data_raw"; // "/imu0"
  string fixed_frame_id = "zed_left_camera_optical_frame"; // "zed_left_camera_optical_frame"; // "map"
};

// CALLBACK WRAPPER CLASS
/* ************************************************************************* */

class Callbacks { 

private:

  LaunchVariables lv;
  
  int frame = 0;

  // Hold node handle initialized in main
  shared_ptr<ros::NodeHandle> nh_ptr;

  // Create a Factor Graph and Values to hold the new data
  NonlinearFactorGraph graph;
  Values initial_estimate; // initial estimates of feature locations (factors)

  // --> Create iSAM2 object
  unique_ptr<ISAM2> isam;

  // Camera calibration intrinsics
  double f;
  double cx;
  double cy;
  
  // Image distortion intrinsics
  double resolution_x;
  double resolution_y;

  // Camera calibration intrinsic matrix
  Cal3_S2Stereo::shared_ptr K;

  // Camera calibration extrinsic
  double Tx; // distance from cam0 to cam1

  // --> Camera observation noise model (has to do with IMU?)
  noiseModel::Isotropic::shared_ptr noise_model = noiseModel::Isotropic::Sigma(2, 1.0); // one pixel in u and v

  // Publish PointCloud messages
  ros::Publisher feature_cloud_pub; 

public:
 
  Callbacks(shared_ptr<ros::NodeHandle> nh_ptr_copy) : nh_ptr(move(nh_ptr_copy)) {

    // initialize PointCloud publisher
    this->feature_cloud_pub = nh_ptr->advertise<sensor_msgs::PointCloud2>("isam2_feature_point_cloud", 1000);

    // YAML intrinsics (pinhole): [fu fv pu pv]
    vector<double> cam0_intrinsics(4);
    nh_ptr->getParam("cam0/intrinsics", cam0_intrinsics); // <- neglect right camera 
    this->f = (cam0_intrinsics[0] + cam0_intrinsics[1]) / 2;
    this->cx = cam0_intrinsics[2];  
    this->cy = cam0_intrinsics[3];
    
//    // YAML image distortion parameters (radtan): [k1 k2 r1 r2]
    vector<double> cam0_resolution(2);
//    vector<double> cam0_distortion_coeffs(4);
    nh_ptr->getParam("cam0/resolution", cam0_resolution); // <- neglect right camera
//    nh_ptr->getParam("cam0/distortion_coeffs", cam0_distortion_coeffs);
//    double k1 = cam0_distortion_coeffs[0];
//    double k2 = cam0_distortion_coeffs[1];
//    double r1 = cam0_distortion_coeffs[2];
//    double r2 = cam0_distortion_coeffs[3];
    double image_scaling = 1; // take into account scaling
    this->resolution_x =  cam0_resolution[0] * image_scaling;
    this->resolution_y =  cam0_resolution[1] * image_scaling;
    
    // YAML extrinsics (distance between 2 cameras)
    vector<double> T_cam1(16);
    nh_ptr->getParam("cam1/T_cn_cnm1", T_cam1);
    this->Tx = T_cam1[3];
    ROS_INFO("cam1/T_cn_cnm1 exists? %d", nh_ptr->hasParam("cam1/T_cn_cnm1"));
    
    // set K: (fx, fy, s, u0, v0, b) (b: baseline where Z = f*d/b; Tx is negative)
    this->K.reset(new Cal3_S2Stereo(cam0_intrinsics[0], cam0_intrinsics[1], 0.0, 
    this->cx, this->cy, -this->Tx));
    
    // iSAM2 settings
    ISAM2Params parameters;
    parameters.relinearizeThreshold = 0.01;
    parameters.relinearizeSkip = 1;
    isam.reset(new ISAM2(parameters));

    // print to confirm reading the YAML file correctly
    ROS_INFO("cam0/intrinsics exists? %d", nh_ptr->hasParam("cam0/intrinsics")); 
    ROS_INFO("intrinsics: %f, %f, %f, %f", cam0_intrinsics[0], cam0_intrinsics[1], 
      cam0_intrinsics[2], cam0_intrinsics[3]);
    ROS_INFO("Tx: %f", Tx);
  }

  void callback(const CameraMeasurementConstPtr& camera_msg, const ImuConstPtr& imu_msg) {

    // initial estimate for camera pose at current frame
    initial_estimate.insert(Symbol('x', frame), Pose3());

    // create object to publish PointCloud estimates of features in this frame
    pcl::PointCloud<pcl::PointXYZ>::Ptr feature_cloud_msg_ptr(new pcl::PointCloud<pcl::PointXYZ>());

    // retrieved subscribed features ids and (u,v) image locations from ImageProcessor
    vector<FeatureMeasurement> feature_vector = camera_msg->features;  

    for (int i = 0; i < feature_vector.size(); i++) {

      // estimate world coor of feature, add to initial_estimate, and add to PointCloud 
      Point3 world_point = processFeature(feature_vector[i], feature_cloud_msg_ptr);
      
      if (frame == 0 && i == 0) {
        
        if (world_point == Point3()) { // (0,0,0): default position when no feature found
          ROS_WARN("No features in first frame");
        }
        
        // Add a prior on landmark l0 since seen in pose x0 (frame 0) which is reference camera frame
        noiseModel::Isotropic::shared_ptr point_noise = noiseModel::Isotropic::Sigma(3, 0.1);
        graph.emplace_shared<PriorFactor<Point3> >(Symbol('l', feature_vector[i].id), world_point, point_noise);
      }
      
    }
          
    if (frame == 0) {

      // Add a prior on pose x0: zero pose
      noiseModel::Diagonal::shared_ptr pose_noise = noiseModel::Diagonal::Sigmas((Vector(6) << Vector3::Constant(0.3),Vector3::Constant(0.1)).finished()); // 30cm std on x,y,z 0.1 rad on roll,pitch,yaw 
      graph.emplace_shared<PriorFactor<Pose3> >(Symbol('x', 0), Pose3(), pose_noise);

    } else {
    
      // Update iSAM with the new factors
//      isam->update(graph, initial_estimate); // causes segmentation fault

//      // Each call to iSAM2 update(*) performs one iteration of the iterative nonlinear solver.
//      // If accuracy is desired at the expense of time, update(*) can be called additional times
//      // to perform multiple optimizer iterations every step.
//      isam->update();

//      // estimate for current frame
//      Values current_estimate = isam->calculateEstimate();
//      // Print Results to ROS_INFO with currentEstimate.print("Current estimate: ");

      // Clear the factor graph and values for the next iteration
      graph.resize(0);
      initial_estimate.clear(); 
    }

    // publish feature PointCloud messages
    feature_cloud_msg_ptr->header.frame_id = lv.fixed_frame_id;
    feature_cloud_msg_ptr->height = 1;
    feature_cloud_msg_ptr->width = feature_cloud_msg_ptr->points.size();
    this->feature_cloud_pub.publish(feature_cloud_msg_ptr); 

    // find centroid position of PointCloud
    Eigen::Matrix<double,4,1> centroid;
    pcl::compute3DCentroid(*feature_cloud_msg_ptr, centroid);

    ROS_INFO("frame %d, %lu total features, centroid: (%f, %f, %f)", frame, feature_vector.size(), centroid[0], centroid[1], centroid[2]);

    frame++;
  }

  Point3 processFeature(FeatureMeasurement feature, pcl::PointCloud<pcl::PointXYZ>::Ptr feature_cloud_msg_ptr) {

    // initialize world coordinate
    Point3 world_point;

    // identify feature (may appear in previous/future frames)
    int l = feature.id;

    double uL = (feature.u0 + 1) * 0.5 * resolution_x;
    double uR = (feature.u1 + 1) * 0.5 * resolution_x ;
    double v = ((feature.v0 + feature.v1) / 2.0 + 1) * 0.5 * resolution_y;

    double d = uR - uL;
    double x = uL;
    double y = v;
    double W = d / this->Tx;

    // estimated feature location in camera frame
    double X_camera = (x - cx) / W;
    double Y_camera = (y - cy) / W;
    double Z_camera = this->f / W; 
    Point3 camera_point = Point3(X_camera, Y_camera, Z_camera);
    
    // add location in camera frame to PointCloud
    pcl::PointXYZ pcl_camera_point = pcl::PointXYZ (camera_point.x(), camera_point.y(), camera_point.z());
    feature_cloud_msg_ptr->points.push_back(pcl_camera_point); 
    
    // update ISAM2
    graph.emplace_shared<
      GenericStereoFactor<Pose3, Point3> >(StereoPoint2(uL, uR, v),
        noise_model, Symbol('x', frame), Symbol('l', l), K);

		// add initial estimate of landmark if it hasn't appeared yet
    if (!initial_estimate.exists(Symbol('l', l))) {
  		Pose3 cam_pose = initial_estimate.at<Pose3>(Symbol('x', frame));
  		world_point = cam_pose.transform_from(camera_point); 
  		initial_estimate.insert(Symbol('l', l), world_point); 
    }		

    return world_point;
  }

};

// MAIN
/* ************************************************************************* */
int main(int argc, char **argv) {

  LaunchVariables lv;

  ros::init(argc, argv, "isam2"); // specify name of node and ROS arguments
  shared_ptr<ros::NodeHandle> nh_ptr = make_shared<ros::NodeHandle>();

  // Instantiate class containing callbacks and necessary variables
  Callbacks callbacks_obj(nh_ptr);

  // Subscribe to "features" and "imu" topics simultaneously
  // zed: /minitaur/image_processor/features
  message_filters::Subscriber<CameraMeasurement> feature_sub(*nh_ptr, lv.feature_topic_id, 1); 
  // zed: /zed/imu/data_raw
  message_filters::Subscriber<Imu> imu_sub(*nh_ptr, lv.imu_topic_id, 1); 
  TimeSynchronizer<CameraMeasurement, Imu> sync(feature_sub, imu_sub, 10);
  sync.registerCallback(boost::bind(&Callbacks::callback, &callbacks_obj, _1, _2));

  // Loop, pumping all callbacks (specified in subscriber object)
  ros::spin(); 

  return 0;
}

