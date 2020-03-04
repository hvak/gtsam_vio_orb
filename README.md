# SLAM for ZED Stereo Camera using ISAM2

## Preliminaries

This package uses: 
* the isam2 and factor graph tools from [gtsam] (https://github.com/borglab/gtsam).
* the feature tracker and image processor nodelet included with [msckf_vio] (https://github.com/KumarRobotics/msckf_vio).

When building gtsam from source, use the following cmake flags: -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF -DGTSAM_BUILD_TESTS=OFF -DGTSAM_BUILD_UNSTABLE=OFF -DGTSAM_BUILD_WRAP=OFF -DGTSAM_USE_SYSTEM_EIGEN=ON -DGTSAM_TYPEDEF_POINTS_TO_VECTORS=ON

## Instructions to Change Launch Specifications

To change which isam2 node is being run:
change "isam2_node" variable in CMakeLists.txt to one of the below (e.g. "isam2_vio_zedpose")
- isam2_vio_zedpose - run ISAM2 for combined CAMERA VIO and ZED ODOMETRY OUTPUT 
- isam2_vio - run ISAM2 for CAMERA VIO alone
- isam2_vio_imu - run ISAM2 for combined CAMERA VIO and RAW ZED IMU OUTPUT (bad performance)
- isam2_imu - run ISAM2 for RAW ZED IMU OUTPUT alone (bad performance)

To change frame and camera topic specifications:
- change "ISAM2 Variables" and "ISAM2 Parameters" at the top of isam2_minitaur_zed.launch

## Instructions to Run 

Run launch file:
- roslaunch gtsam_vio isam2_minitaur_zed.launch

For the launch file to work, the following topics should be publishing messages:
- /zed/zed_node/left/image_rect_color/compressed
- /zed/zed_node/right/image_rect_color/compressed
- /zed/zed_node/odom
- /zed/zed_node/imu/data

The following topics may be helpful:
- /zed/zed_node/left/camera_info
- /zed/zed_node/right/camera_info

## Instructions to Visualize

To visualize the estimated camera pose and 3D locations of features in the world frame, run the following command:
- rviz rviz -d ~/catkin_ws/src/gtsam_vio/rviz/rviz_tf_features_config.rviz 
