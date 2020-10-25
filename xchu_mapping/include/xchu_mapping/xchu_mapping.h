

#ifndef NDT_MAPPING_LIDARMAPPING_H
#define NDT_MAPPING_LIDARMAPPING_H

#define OUTPUT  // define OUTPUT if you want to log postition.txt

#include <ros/ros.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <nav_msgs/Path.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/PointCloud2.h>
#include <std_msgs/Float32.h>
#include <std_msgs/Bool.h>

#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>

#include <pcl/io/io.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl_conversions/pcl_conversions.h>

#include <pcl/registration/ndt.h>
#include <ndt_cpu/NormalDistributionsTransform.h>
//#ifdef CUDA_FOUND
//#include <ndt_gpu/NormalDistributionsTransform.h>
//#endif
#include <pcl_omp_registration/ndt.h>  // if USE_PCL_OPENMP
#include <pclomp/ndt_omp.h>
#include "omp.h"
#include <ctime>
#include <deque>
#include <mutex>
#include <queue>
using PointI = pcl::PointXYZI;

struct pose {
  double x;
  double y;
  double z;
  double roll;
  double pitch;
  double yaw;

  void init() {
    x = y = z = 0.0;
    roll = pitch = yaw = 0.0;
  }

  Eigen::Matrix4d rotateRPY() {
    Eigen::Translation3d tf_trans(x, y, z);
    Eigen::AngleAxisd rot_x(roll, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd rot_y(pitch, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd rot_z(yaw, Eigen::Vector3d::UnitZ());
    Eigen::Matrix4d mat = (tf_trans * rot_z * rot_y * rot_x).matrix();
    return mat;
  }
};

enum class MethodType {
  use_pcl = 0,
  use_cpu = 1,
  use_gpu = 2,
  use_omp = 3,
  use_gpu_ptr = 4,
};

static MethodType _method_type = MethodType::use_cpu;  // 默认使用cpu运算

static bool _incremental_voxel_update = false;

static ros::Time callback_start, callback_end, t1_start, t1_end, t2_start, t2_end, t3_start, t3_end, ndt_start, ndt_end,
    t5_start, t5_end;
static ros::Duration d_callback, d1, d2, d3, d4, d5;

static int submap_num = 0;
static double submap_size = 0.0;
static double max_submap_size, odom_size;

static pclomp::NormalDistributionsTransform<pcl::PointXYZI, pcl::PointXYZI>::Ptr omp_ndt;

class LidarMapping {
 public:
  LidarMapping();

  inline double warpToPm(double a_num, const double a_max) {
    if (a_num >= a_max) {
      a_num -= 2.0 * a_max;
    }
    return a_num;
  }

  inline double warpToPmPi(double a_angle_rad) {
    return warpToPm(a_angle_rad, M_PI);
  }

  inline double calcDiffForRadian(const double lhs_rad, const double rhs_rad) {
    double diff_rad = lhs_rad - rhs_rad;
    if (diff_rad >= M_PI)
      diff_rad = diff_rad - 2 * M_PI;
    else if (diff_rad < -M_PI)
      diff_rad = diff_rad + 2 * M_PI;
    return diff_rad;
  }

  /**
   * 程序主流程
   */
  void run();

  /**
   * 点云处理和匹配
   * @param input
   * @param current_scan_time
   */
  void process_cloud(const pcl::PointCloud<pcl::PointXYZI> &input, const ros::Time &current_scan_time);

  void param_initial(ros::NodeHandle &private_handle);

  void imu_callback(const sensor_msgs::Imu::Ptr &input);

  void odom_callback(const nav_msgs::Odometry::ConstPtr &input);

  void points_callback(const sensor_msgs::PointCloud2::ConstPtr &input);

  void imu_odom_calc(ros::Time current_time);

  void imu_calc(ros::Time current_time);

  void odom_calc(ros::Time current_time);

  void imuUpSideDown(const sensor_msgs::Imu::Ptr input);

  void odom_info(const nav_msgs::Odometry &input);

  void imu_info(const sensor_msgs::Imu &input);

 private:
  pose previous_pose, guess_pose, guess_pose_imu, guess_pose_odom, guess_pose_imu_odom;
  pose current_pose, current_pose_imu, current_pose_odom, current_pose_imu_odom;
  pose ndt_pose, localizer_pose;
  pose added_pose;  // added_pose记录点云加入地图时候的位置  // 初始设为0即可,因为第一帧如论如何也要加入地图的

  ros::Time current_scan_time;
  ros::Time previous_scan_time;
  ros::Duration scan_duration;

  // 定义Publisher
  ros::Publisher ndt_map_pub;        // 地图发布
  ros::Publisher local_map_pub;        // 地图发布
  ros::Publisher current_odom_pub;        // 地图发布
  ros::Publisher current_pose_pub;   // 位置发布
  ros::Publisher guess_pose_linaer_pub;  // TODO:这是个啥发布????
  ros::Publisher current_points_pub, path_pub;  // TODO:这是个啥发布????


  ros::Subscriber points_sub;
  ros::Subscriber imu_sub;
  ros::Subscriber odom_sub;
  geometry_msgs::PoseStamped current_pose_msg, guess_pose_msg;

  ros::Publisher ndt_stat_pub;
  std_msgs::Bool ndt_stat_msg;  // 确认是否是ndt的第一帧图像 bool类型

  // 设置变量,用以接受ndt配准后的参数
  double fitness_score;
  bool has_converged;
  int final_num_iteration;
  double transformation_probability;

  // 设置变量,用以接受imu和odom消息
  sensor_msgs::Imu imu;
  nav_msgs::Odometry odom;

  // 读写文件流相关
  std::ofstream ofs; // 写csv文件
  std::string filename;

  MethodType _method_type;

  // 定义各种差异值(两次采集数据之间的差异,包括点云位置差异,imu差异,odom差异,imu-odom差异)
  double diff;
  double diff_x, diff_y, diff_z, diff_yaw;  // current_pose - previous_pose // 定义两帧点云差异值 --以确定是否更新点云等
  double offset_imu_x, offset_imu_y, offset_imu_z, offset_imu_roll, offset_imu_pitch, offset_imu_yaw;
  double offset_odom_x, offset_odom_y, offset_odom_z, offset_odom_roll, offset_odom_pitch, offset_odom_yaw;
  double offset_imu_odom_x, offset_imu_odom_y, offset_imu_odom_z, offset_imu_odom_roll, offset_imu_odom_pitch,
      offset_imu_odom_yaw;

  // 定义速度值 --包括实际速度值,和imu取到的速度值
  double current_velocity_x;
  double current_velocity_y;
  double current_velocity_z;

  double current_velocity_imu_x;
  double current_velocity_imu_y;
  double current_velocity_imu_z;

  pcl::PointCloud<pcl::PointXYZI> map, globalmap;  // 此处定义地图  --global map
  pcl::PointCloud<pcl::PointXYZI> submap;  // 此处定义地图  --global map
  pcl::PointCloud<pcl::PointXYZI> dense_map;  // 此处定义地图  --global map

  pcl::NormalDistributionsTransform<pcl::PointXYZI, pcl::PointXYZI> pcl_ndt;
  cpu::NormalDistributionsTransform<pcl::PointXYZI, pcl::PointXYZI> cpu_ndt;  // cpu方式  --可以直接调用吗??可以
#ifdef CUDA_FOUND
  gpu::GNormalDistributionsTransform gpu_ndt;
  // TODO:此处增加共享内存方式的gpu_ndt_ptr
   std::shared_ptr<gpu::GNormalDistributionsTransform> gpu_ndt = std::make_shared<gpu::GNormalDistributionsTransform>();
#endif
//#ifdef USE_PCL_OPENMP  // 待查证使用方法
//		static pcl_omp::NormalDistributionsTransform<pcl::PointXYZI, pcl::PointXYZI> omp_ndt;
//#endif
  //pcl_omp::NormalDistributionsTransform<pcl::PointXYZI, pcl::PointXYZI> omp_ndt;
//  pclomp::NormalDistributionsTransform<pcl::PointXYZI, pcl::PointXYZI>::Ptr omp_ndt;

  // Default values  // 公共ndt参数设置
  int max_iter;        // Maximum iterations
  float ndt_res;      // Resolution
  double step_size;   // Step size
  double trans_eps;  // Transformation epsilon

  // Leaf size of VoxelGrid filter.   ---该处定义的是一个默认的栅格大小
  double voxel_leaf_size;

  Eigen::Matrix4f gnss_transform;  // 保存GPS信号的变量

  double min_scan_range;  // min和max用于第一次进行点云过滤(截取两个同心圆内的)
  double max_scan_range;
  double min_add_scan_shift;  // 定义将点云添加到locaMap里的最小间隔值  --应该是添加到localMap吧??

  int initial_scan_loaded;

  double _tf_x, _tf_y, _tf_z, _tf_roll, _tf_pitch, _tf_yaw;
  Eigen::Matrix4f tf_btol, tf_ltob;  // ?????

  // 重要变量参数 :: 用以指示是否使用imu,是否使用odom
  bool _use_imu;
  bool _use_odom;
  bool _imu_upside_down;  // 用以解决坐标系方向(正负变换)问题 (比如x变更为-x等)


  std::string _imu_topic;  // 定义imu消息的topic
  std::string _odom_topic;
  std::string _lidar_topic;
  std::string map_saved_dir;

  // end:private
  ros::NodeHandle nh_, privateHandle;

  // mutex
  std::mutex mutex_lock;
  std::queue<sensor_msgs::Imu> imuBuf;
  std::queue<sensor_msgs::PointCloud2> cloudBuf;
  std::queue<nav_msgs::Odometry> odomBuf;
};

#endif //NDT_MAPPING_LIDARMAPPING_H
