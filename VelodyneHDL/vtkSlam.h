//=========================================================================
//
// Copyright 2018 Kitware, Inc.
// Author: Guilbert Pierre (spguilbert@gmail.com)
// Data: 03-27-2018
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//=========================================================================

// This slam algorithm is largely inspired by the LOAM algorithm:
// J. Zhang and S. Singh. LOAM: Lidar Odometry and Mapping in Real-time.
// Robotics: Science and Systems Conference (RSS). Berkeley, CA, July 2014.

// The algorithm is composed of three sequential steps:
//
// - Keypoints extraction: this step consists of extracting keypoints over
// the points clouds. To do that, the laser lines / scans are trated indepently.
// The laser lines are projected onto the XY plane and are rescale depending on
// their vertical angle. Then we compute their curvature and create two class of
// keypoints. The edges keypoints which correspond to points with a hight curvature
// and planar points which correspond to points with a low curvature.
//
// - Ego-Motion: this step consists of recovering the motion of the lidar
// sensor between two frames (two sweeps). The motion is modelized by a constant
// velocity and angular velocity between two frames (i.e null acceleration). 
// Hence, we can parameterize the motion by a rotation and translation per sweep / frame
// and interpolate the transformation inside a frame using the timestamp of the points.
// Since the points clouds generated by a lidar are sparses we can't design a
// pairwise match between keypoints of two successive frames. Hence, we decided to use
// a closest-point matching between the keypoints of the current frame
// and the geometrics features derived from the keypoints of the previous frame.
// The geometrics features are lines or planes and are computed using the edges keypoints
// and planar keypoints of the previous frame. Once the matching is done, a keypoint
// of the current frame is matched with a plane / line (depending of the
// nature of the keypoint) from the previous frame. Then, we recover R and T by
// minimizing the function f(R, T) = sum(d(point, line)^2) + sum(d(point, plane)^2).
// Which can be writen f(R, T) = sum((R*X+T-P).t*A*(R*X+T-P)) where:
// - X is a keypoint of the current frame
// - P is a point of the corresponding line / plane
// - A = (n*n.t) with n being the normal of the plane
// - A = (I - n*n.t).t * (I - n*n.t) with n being a director vector of the line
// Since the function f(R, T) is a non-linear mean square error function
// we decided to use the Levenberg-Marquardt algorithm to recover its argmin.
//
// - Mapping: This step consists of refining the motion recovered in the Ego-Motion
// step and to add the new frame in the environment map. Thanks to the ego-motion
// recovered at the previous step it is now possible to estimate the new position of
// the sensor in the map. We use this estimation as an initial point (R0, T0) and we
// perform an optimization again using the keypoints of the current frame and the matched
// keypoints of the map (and not only the previous frame this time!). Once the position in the
// map has been refined from the first estimation it is then possible to update the map by
// adding the keypoints of the current frame into the map.
//
// In the following programs : "vtkSlam.h" and "vtkSlam.cxx" the lidar
// coordinate system {L} is a 3D coordinate system with its origin at the
// geometric center of the lidar. The world coordinate system {W} is a 3D
// coordinate system which coinciding with {L] at the initial position. The
// points will be denoted by the ending letter L or W if they belong to
// the corresponding coordinate system

#ifndef VTK_SLAM_H
#define VTK_SLAM_H

#define slamGetMacro(prefix,name,type) \
type Get##prefix##_##name () const\
  { \
  return this->name; \
  }

#define slamSetMacro(prefix,name,type) \
void Set##prefix##_##name (const type _arg) \
{ \
  this->name = _arg; \
}

// LOCAL
#include "vtkPCLConversions.h"
// STD
#include <string>
#include <ctime>
// VTK
#include <vtkPolyDataAlgorithm.h>
#include <vtkSmartPointer.h>
// EIGEN
#include <Eigen/Dense>
// PCL
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>


class vtkVelodyneTransformInterpolator;
class RollingGrid;
typedef pcl::PointXYZINormal Point;

class KalmanFilter
{
public:
  // default constructor
  KalmanFilter();

  // Reset the class
  void ResetKalmanFilter();

  // Set current time of the algorithm
  void SetCurrentTime(double time);

  // Prediction of the next state vector
  void Prediction();

  // Correction of the prediction using
  // the input measure
  void Correction(Eigen::MatrixXd Measure);

  // Set the measures variance covariance matrix
  void SetMeasureCovariance(Eigen::MatrixXd argCov);

  // Set the maximum angle acceleration
  // use to compute variance covariance matrix
  void SetMaxAngleAcceleration(double acc);

  // Set the maximum velocity acceleration
  // use to compute variance covariance matrix
  void SetMaxVelocityAcceleration(double acc);

  // return the state vector
  Eigen::Matrix<double, 12, 1> GetStateVector();

  // Initialize the state vector and the covariance-variance
  // estimation
  void SetInitialStatevector(Eigen::Matrix<double, 12, 1> iniVector, Eigen::Matrix<double, 12, 12> iniCov);

  // set the kalman filter mode
  void SetMode(int argMode);
  int GetMode();

  // return the number of observed measures
  int GetNbrMeasure();

private:
  // Kalman Filter mode:
  // 0 : Motion Model
  // 1 : Motion Model + GPS velocity
  int mode;

  // Motion model / Prediction Model
  Eigen::Matrix<double, 12, 12> MotionModel;

  // Link between the measures and the state vector
  Eigen::MatrixXd MeasureModel;

  // Variance-Covariance of measures
  Eigen::MatrixXd MeasureCovariance;

  // Variance-Covariance of model
  Eigen::Matrix<double, 12, 12> ModelCovariance;

  // State vector composed like this:
  // -rx, ry, rz
  // -tx, ty, tz
  // -drx/dt, dry/dt, drz/dt
  // -dtx/dt, dty/dt, dtz/dt
  Eigen::Matrix<double, 12, 1> VectorState;
  Eigen::Matrix<double, 12, 1> VectorStatePredicted;

  // Estimator variance covariance
  Eigen::Matrix<double, 12, 12> EstimatorCovariance;

  // delta time for prediction
  double PreviousTime;
  double CurrentTime;
  double DeltaTime;

  // Maximale acceleration endorsed by the vehicule
  double MaxAcceleration;
  double MaxAngleAcceleration;

  // indicate the number of observed measures
  unsigned int NbrMeasures;
};

class VTK_EXPORT vtkSlam : public vtkPolyDataAlgorithm
{
public:
  // vtkPolyDataAlgorithm functions
  static vtkSlam *New();
  vtkTypeMacro(vtkSlam, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent);

  // Add a new frame to process to the slam algorithm
  // From this frame; keypoints will be computed and extracted
  // in order to recover the ego-motion of the lidar sensor
  // and to update the map using keypoints and ego-motion
  void AddFrame(vtkPolyData* newFrame);

  // Reset the algorithm. Notice that this function
  // will erase the map and all transformations that
  // have been computed so far
  void ResetAlgorithm();

  // output the parameters value of the slam algorithm
  void PrintParameters();

  // Provide the calibration of the current sensor.
  // The mapping indicates the number of laser and
  // the mapping of the laser id
  void SetSensorCalibration(int* mapping, int nbLaser);

  // Indicate if the sensor calibration: number
  // of lasers and mapping of the laser id has been
  // provided earlier
  bool GetIsSensorCalibrationProvided();

  // Get the computed world transform so far
  void GetWorldTransform(double* Tworld);

  // Only compute the keypoint extraction to display result
  // This function is usefull for debugging
  void OnlyComputeKeypoints(vtkSmartPointer<vtkPolyData> newFrame);

  // Get/Set General
  slamGetMacro(,DisplayMode, bool)
  slamSetMacro(,DisplayMode, bool)

  slamGetMacro(,MaxDistBetweenTwoFrames, double)
  slamSetMacro(,MaxDistBetweenTwoFrames, double)

  slamGetMacro(,AngleResolution, double)
  slamSetMacro(,AngleResolution, double)

  slamGetMacro(,MaxDistanceForICPMatching, double)
  slamSetMacro(,MaxDistanceForICPMatching, double)

  slamGetMacro(,Lambda0, double)
  slamSetMacro(,Lambda0, double)

  slamGetMacro(,LambdaRatio, double)
  slamSetMacro(,LambdaRatio, double)

  slamGetMacro(,FastSlam, bool)
  slamSetMacro(,FastSlam, bool)

  slamGetMacro(,Undistortion, bool)
  slamSetMacro(,Undistortion, bool)

  // set the motion model
  void SetMotionModel(int input);

  void SetMaxVelocityAcceleration(double acc);
  void SetMaxAngleAcceleration(double acc);

  // Get/Set RollingGrid
  /*const*/ unsigned int Get_RollingGrid_VoxelSize() const;
  void Set_RollingGrid_VoxelSize(const unsigned int size);

  void Get_RollingGrid_Grid_NbVoxel(double nbVoxel[3]) const;
  void Set_RollingGrid_Grid_NbVoxel(const double nbVoxel[3]);

  void Get_RollingGrid_PointCloud_NbVoxel(double nbVoxel[3]) const;
  void Set_RollingGrid_PointCloud_NbVoxel(const double nbVoxel[3]);

  /*const*/ double Get_RollingGrid_LeafVoxelFilterSize() const;
  void Set_RollingGrid_LeafVoxelFilterSize(const double size);

  // Get/Set Keypoint
  slamGetMacro(_Keypoint,MaxEdgePerScanLine, unsigned int)
  slamSetMacro(_Keypoint,MaxEdgePerScanLine, unsigned int)

  slamGetMacro(_Keypoint,MaxPlanarsPerScanLine, unsigned int)
  slamSetMacro(_Keypoint,MaxPlanarsPerScanLine, unsigned int)

  slamGetMacro(_Keypoint,MinDistanceToSensor, double)
  slamSetMacro(_Keypoint,MinDistanceToSensor, double)

  slamGetMacro(_Keypoint,EdgeSinAngleThreshold, double)
  slamSetMacro(_Keypoint,EdgeSinAngleThreshold, double)

  slamGetMacro(_Keypoint,PlaneSinAngleThreshold, double)
  slamSetMacro(_Keypoint,PlaneSinAngleThreshold, double)

  slamGetMacro(_Keypoint,EdgeDepthGapThreshold, double)
  slamSetMacro(_Keypoint,EdgeDepthGapThreshold, double)

  // Get/Set EgoMotion
  slamGetMacro(,EgoMotionMaxIter, unsigned int)
  slamSetMacro(,EgoMotionMaxIter, unsigned int)

  slamGetMacro(,EgoMotionIcpFrequence, unsigned int)
  slamSetMacro(,EgoMotionIcpFrequence, unsigned int)

  slamGetMacro(,EgoMotionLineDistanceNbrNeighbors, unsigned int)
  slamSetMacro(,EgoMotionLineDistanceNbrNeighbors, unsigned int)

  slamGetMacro(,EgoMotionMinimumLineNeighborRejection, unsigned int)
  slamSetMacro(,EgoMotionMinimumLineNeighborRejection, unsigned int)

  slamGetMacro(,EgoMotionLineDistancefactor, double)
  slamSetMacro(,EgoMotionLineDistancefactor, double)

  slamGetMacro(,EgoMotionPlaneDistanceNbrNeighbors, unsigned int)
  slamSetMacro(,EgoMotionPlaneDistanceNbrNeighbors, unsigned int)

  slamGetMacro(,EgoMotionPlaneDistancefactor1, double)
  slamSetMacro(,EgoMotionPlaneDistancefactor1, double)

  slamGetMacro(,EgoMotionPlaneDistancefactor2, double)
  slamSetMacro(,EgoMotionPlaneDistancefactor2, double)

  slamGetMacro(,EgoMotionMaxLineDistance, double)
  slamSetMacro(,EgoMotionMaxLineDistance, double)

  slamGetMacro(,EgoMotionMaxPlaneDistance, double)
  slamSetMacro(,EgoMotionMaxPlaneDistance, double)

  // Get/Set Mapping
  slamGetMacro(,MappingMaxIter, unsigned int)
  slamSetMacro(,MappingMaxIter, unsigned int)

  slamGetMacro(,MappingIcpFrequence, unsigned int)
  slamSetMacro(,MappingIcpFrequence, unsigned int)

  slamGetMacro(,MappingLineDistanceNbrNeighbors, unsigned int)
  slamSetMacro(,MappingLineDistanceNbrNeighbors, unsigned int)

  slamGetMacro(,MappingMinimumLineNeighborRejection, unsigned int)
  slamSetMacro(,MappingMinimumLineNeighborRejection, unsigned int)

  slamGetMacro(,MappingLineDistancefactor, double)
  slamSetMacro(,MappingLineDistancefactor, double)

  slamGetMacro(,MappingPlaneDistanceNbrNeighbors, unsigned int)
  slamSetMacro(,MappingPlaneDistanceNbrNeighbors, unsigned int)

  slamGetMacro(,MappingPlaneDistancefactor1, double)
  slamSetMacro(,MappingPlaneDistancefactor1, double)

  slamGetMacro(,MappingPlaneDistancefactor2, double)
  slamSetMacro(,MappingPlaneDistancefactor2, double)

  slamGetMacro(,MappingMaxLineDistance, double)
  slamSetMacro(,MappingMaxLineDistance, double)

  slamGetMacro(,MappingMaxPlaneDistance, double)
  slamSetMacro(,MappingMaxPlaneDistance, double)

  slamGetMacro(,MappingLineMaxDistInlier, double)
  slamSetMacro(,MappingLineMaxDistInlier, double)

  // Set transforms information / interpolator from an
  // external sensor (GPS, IMU, Camera SLAM, ...) to be
  // use to aid the SLAM algorithm. Note that without any
  // information about the variance / covariance of the measured
  // the data will only be used to initialize the SLAM odometry
  // and will not be merged with the slam data using a Kalman filter
  void SetExternalSensorMeasures(vtkVelodyneTransformInterpolator* interpolator);

  // Load slam transforms in order to add them in
  // the trajectory polydata. This won't affect the
  // slam algorithm state
  void LoadTransforms(const std::string& filename);

  // return the internal interpolator
  vtkVelodyneTransformInterpolator* GetInterpolator() const;
  void SetInterpolator(vtkVelodyneTransformInterpolator* interpolator, double easting0, double northing0, double height0, int utm);
  void SetInterpolator(vtkVelodyneTransformInterpolator* interpolator);
  void AddGeoreferencingFieldInformation(double easting0, double northing0, double height0, int utm);

  // Export the transforms that have been computed
  void ExportTransforms(const std::string& filename);

protected:
  // vtkPolyDataAlgorithm functions
  vtkSlam();
  ~vtkSlam();
  virtual int RequestData(vtkInformation *, vtkInformationVector **, vtkInformationVector *);
  virtual int RequestDataObject(vtkInformation *, vtkInformationVector **, vtkInformationVector *);
  virtual int RequestInformation(vtkInformation *, vtkInformationVector **, vtkInformationVector *);
  virtual int RequestUpdateExtent(vtkInformation *, vtkInformationVector **, vtkInformationVector * );
private:
  vtkSlam(const vtkSlam&);
  void operator = (const vtkSlam&);
  // Polydata which represents the trajectory computed
  vtkSmartPointer<vtkPolyData> Trajectory;
  vtkSmartPointer<vtkPolyData> Orientation;
  vtkSmartPointer<vtkVelodyneTransformInterpolator> InternalInterp;

  // Current point cloud stored in two differents
  // formats: PCL-pointcloud and vtkPolyData
  vtkSmartPointer<vtkPolyData> vtkCurrentFrame;
  vtkSmartPointer<vtkPolyData> vtkProcessedFrame;
  pcl::PointCloud<Point>::Ptr pclCurrentFrame;
  std::vector<pcl::PointCloud<Point>::Ptr> pclCurrentFrameByScan;
  std::vector<std::pair<int, int> > FromVTKtoPCLMapping;
  std::vector<std::vector<int > > FromPCLtoVTKMapping;

  // If set to true the mapping planars keypoints used
  // will be the same than the EgoMotion one. If set to false
  // all points that are not set to invalid will be used
  // as mapping planars points.
  bool FastSlam;

  // If set to true, the mapping will use a motion
  // model. The motion model will be integrating to
  // ICP estimator using a kalman filter. hence, when
  // the estimation has a poor confidence the slam will
  // use the motion model to improve accuracy
  int MotionModel;

  // Should the algorithm undistord the frame or not
  // The undistortion will improve the accuracy but
  // the computation speed will decrease
  bool Undistortion;

  // keypoints extracted
  pcl::PointCloud<Point>::Ptr CurrentEdgesPoints;
  pcl::PointCloud<Point>::Ptr CurrentPlanarsPoints;
  pcl::PointCloud<Point>::Ptr CurrentBlobsPoints;
  pcl::PointCloud<Point>::Ptr PreviousEdgesPoints;
  pcl::PointCloud<Point>::Ptr PreviousPlanarsPoints;
  pcl::PointCloud<Point>::Ptr PreviousBlobsPoints;
  pcl::PointCloud<Point>::Ptr DensePlanarsPoints;
  pcl::PointCloud<Point>::Ptr MappingPlanarsPoints;
  pcl::PointCloud<Point>::Ptr MappingBlobsPoints;

  // keypoints local map
  RollingGrid* EdgesPointsLocalMap;
  RollingGrid* PlanarPointsLocalMap;
  RollingGrid* BlobsPointsLocalMap;

  // Mapping of the lasers id
  std::vector<int> LaserIdMapping;

  // Curvature and over differntial operations
  // scan by scan; point by point
  std::vector<std::vector<double> > Angles;
  std::vector<std::vector<double> > DepthGap;
  std::vector<std::vector<double> > BlobScore;
  std::vector<std::vector<int> > IsPointValid;
  std::vector<std::vector<int> > Label;

  // Kalman estimator to predict motion
  // using a motion model when the minimization
  // algorithm have a poor parameter prediction
  KalmanFilter KalmanEstimator;

  // with of the neighbor used to compute discrete
  // differential operators
  int NeighborWidth;

  // Number of lasers scan lines composing the pointcloud
  unsigned int NLasers;

  // maximal angle resolution of the lidar
  double AngleResolution;

  // Number of frame that have been processed
  unsigned int NbrFrameProcessed;

  // minimal point/sensor sensor to consider a point as valid
  double MinDistanceToSensor;

  // Indicated the number max of keypoints
  // that we admit per laser scan line
  unsigned int MaxEdgePerScanLine;
  unsigned int MaxPlanarsPerScanLine;

  // Sharpness threshold to select a point
  double EdgeSinAngleThreshold;
  double PlaneSinAngleThreshold;
  double EdgeDepthGapThreshold;

  // The max distance allowed between two frames
  // If the distance is over this limit, the ICP
  // matching will not match point and the odometry
  // will fail. It has to be setted according to the
  // maximum speed of the vehicule used
  double MaxDistBetweenTwoFrames;

  // Maximum number of iteration
  // in the ego motion optimization step
  unsigned int EgoMotionMaxIter;
  unsigned int EgoMotionIterMade;

  // Maximum number of iteration
  // in the mapping optimization step
  unsigned int MappingMaxIter;
  unsigned int MappingIterMade;

  // During the Levenberg-Marquardt algoritm
  // keypoints will have to be match with planes
  // and lines of the previous frame. This parameter
  // indicates how many iteration we want to do before
  // running the closest-point matching again
  unsigned int EgoMotionIcpFrequence;
  unsigned int MappingIcpFrequence;

  // When computing the point<->line and point<->plane distance
  // in the ICP, the kNearest edges/planes points of the current
  // points are selected to approximate the line/plane using a PCA
  // If the one of the k-nearest points is too far the neigborhood
  // is rejected. We also make a filter upon the ratio of the eigen
  // values of the variance-covariance matrix of the neighborhood
  // to check if the points are distributed upon a line or a plane
  unsigned int MappingLineDistanceNbrNeighbors;
  unsigned int MappingMinimumLineNeighborRejection;
  double MappingLineDistancefactor;

  unsigned int MappingPlaneDistanceNbrNeighbors;
  double MappingPlaneDistancefactor1;
  double MappingPlaneDistancefactor2;

  double MappingMaxPlaneDistance;
  double MappingMaxLineDistance;
  double MappingLineMaxDistInlier;

  unsigned int EgoMotionLineDistanceNbrNeighbors;
  unsigned int EgoMotionMinimumLineNeighborRejection;
  double EgoMotionLineDistancefactor;

  unsigned int EgoMotionPlaneDistanceNbrNeighbors;
  double EgoMotionPlaneDistancefactor1;
  double EgoMotionPlaneDistancefactor2;

  double EgoMotionMaxPlaneDistance;
  double EgoMotionMaxLineDistance;

  // norm of the farest keypoints
  double FarestKeypointDist;

  // Use or not blobs
  bool UseBlob;

  // Threshold upon sphricity of a neighborhood
  // to select a blob point
  double SphericityThreshold;

  // Coef to apply to the incertitude
  // radius of the blob neighborhood
  double IncertitudeCoef;

  // Levenberg-Marquardt initial value of lambda
  double Lambda0;

  // Levenberg-Marquardt increase or decrease
  // lambda factor ratio to switch between
  // Gauss-Newton or gradient descent algorithm
  double LambdaRatio;

  // The max distance allowed between two frames
  // If the distance is over this limit, the ICP
  // matching will not match point and the odometry
  // will fail. It has to be setted according to the
  // maximum speed of the vehicule used
  double MaxDistanceForICPMatching;

  // Transformation to map the current pointcloud
  // in the referential of the previous one
  Eigen::Matrix<double, 6, 1> Trelative;

  // Transformation to map the current pointcloud
  // in the world (i.e first frame) one
  Eigen::Matrix<double, 6, 1> Tworld;
  Eigen::Matrix<double, 6, 1> PreviousTworld;

  // Computed trajectory of the sensor
  // i.e the list of transforms computed
  std::vector<Eigen::Matrix<double, 6, 1> > TworldList;

  // external sensor (GPS, IMU, Camera SLAM, ...) to be
  // use to aid the SLAM algorithm. Note that without any
  // information about the variance / covariance of the measured
  // the data will only be used to initialize the SLAM odometry
  // and will not be merged with the slam data using a Kalman filter
  vtkSmartPointer<vtkVelodyneTransformInterpolator> ExternalMeasures;
  double VelocityNormCov;
  bool shouldBeRawTime;
  double CurrentTime;

  // Add a default point to the trajectories
  void AddDefaultPoint(double x, double y, double z, double rx, double ry, double rz, double t);

  // Convert the input vtk-format pointcloud
  // into a pcl-pointcloud format. scan lines
  // will also be sorted by their vertical angles
  void ConvertAndSortScanLines(vtkSmartPointer<vtkPolyData> input);

  // Extract keypoints from the pointcloud. The key points
  // will be separated in two classes : Edges keypoints which
  // correspond to area with high curvature scan lines and
  // planar keypoints which have small curvature
  void ComputeKeyPoints(vtkSmartPointer<vtkPolyData> input);

  // Compute the curvature of the scan lines
  // The curvature is not the one of the surface
  // that intersected the lines but the curvature
  // of the scan lines taken in an isolated way
  void ComputeCurvature(vtkSmartPointer<vtkPolyData> input);

  // Invalid the points with bad criteria from
  // the list of possible future keypoints.
  // This points correspond to planar surface
  // roughtly parallel to laser beam and points
  // close to a gap created by occlusion
  void InvalidPointWithBadCriteria();

  // Labelizes point to be a keypoints or not
  void SetKeyPointsLabels(vtkSmartPointer<vtkPolyData> input);

  // Add Transform to the interpolator
  void AddTransform(double time);
  void AddTransform(double rx, double ry, double rz, double tx, double ty, double tz, double t);

  // Reset all mumbers variables that are
  // used during the process of a frame.
  // The map and the recovered transformations
  // won't be reset.
  void PrepareDataForNextFrame();

  // Find the ego motion of the sensor between
  // the current frame and the next one using
  // the keypoints extracted.
  void ComputeEgoMotion();

  // Map the position of the sensor from
  // the current frame in the world referential
  // using the map and the keypoints extracted.
  void Mapping();

  // Transform the input point acquired at time t1 to the
  // initial time t0. So that the deformation induced by
  // the motion of the sensor will be removed. We use the assumption
  // of constant angular velocity and velocity.
  void TransformToStart(Point& pi, Point& pf, Eigen::Matrix<double, 6, 1>& T);
  void TransformToStart(Eigen::Matrix<double, 3, 1>& Xi, Eigen::Matrix<double, 3, 1>& Xf, double s, Eigen::Matrix<double, 6, 1>& T);

  // Transform the input point acquired at time t1 to the
  // final time tf. So that the deformation induced by
  // the motion of the sensor will be removed. We use the assumption
  // of constant angular velocity and velocity.
  void TransformToEnd(Point& pi, Point& pf, Eigen::Matrix<double, 6, 1>& T);

  // All points of the current frame has been
  // acquired at a different timestamp. The goal
  // is to express them in a same referential
  // corresponding to the referential the end of the sweep.
  // This can be done using estimated egomotion and assuming
  // a constant angular velocity and velocity during a sweep
  void TransformCurrentKeypointsToEnd();

  // Transform the input point already undistort into Tworld.
  void TransformToWorld(Point& p, Eigen::Matrix<double, 6, 1>& T);

  // From the input point p, find the nearest edge line from
  // the previous point cloud keypoints
  void FindEdgeLineMatch(Point p, pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousEdges,
                         std::vector<int>& matchEdgeIndex1, std::vector<int>& matchEdgeIndex2, int currentEdgeIndex,
                         Eigen::Matrix<double, 3, 3> R, Eigen::Matrix<double, 3, 1> dT);

  // From the input point p, find the nearest plane from the
  // previous point cloud keypoints that match the input point
  void FindPlaneMatch(Point p, pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousPlanes,
                      std::vector<int>& matchPlaneIndex1, std::vector<int>& matchPlaneIndex2,
                      std::vector<int>& matchPlaneIndex3, int currentPlaneIndex,
                      Eigen::Matrix<double, 3, 3> R, Eigen::Matrix<double, 3, 1> dT);

  // From the line / plane match of the current keypoint, compute
  // the parameters of the distance function. The distance function is
  // (R*X+T - P).t * A * (R*X+T - P). These functions will compute the
  // parameters P and A.
  void ComputeLineDistanceParameters(std::vector<int>& matchEdgeIndex1, std::vector<int>& matchEdgeIndex2, unsigned int edgeIndex);
  void ComputePlaneDistanceParameters(std::vector<int>& matchPlaneIndex1, std::vector<int>& matchPlaneIndex2, std::vector<int>& matchPlaneIndex3, unsigned int planarIndex);

  // More accurate but slower
  void ComputeLineDistanceParametersAccurate(pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousEdges, Eigen::Matrix<double, 3, 3>& R,
                                             Eigen::Matrix<double, 3, 1>& dT, Point p, std::string step);
  void ComputePlaneDistanceParametersAccurate(pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousPlanes, Eigen::Matrix<double, 3, 3>& R,
                                              Eigen::Matrix<double, 3, 1>& dT, Point p, std::string step);
  void ComputeBlobsDistanceParametersAccurate(pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousBlobs, Eigen::Matrix<double, 3, 3>& R,
                                              Eigen::Matrix<double, 3, 1>& dT, Point p, std::string step);

  // we want to minimize F(R,T) = sum(fi(R,T)^2)
  // for a given i; fi is called a residual value and
  // the jacobian of fi is called the residual jacobian
  void ComputeResidualValues(std::vector<Eigen::Matrix<double, 3, 3> >& vA, std::vector<Eigen::Matrix<double, 3, 1> >& vX,
                             std::vector<Eigen::Matrix<double, 3, 1> >& vP, std::vector<double>& vS,
                             Eigen::Matrix<double, 3, 3>& R, Eigen::Matrix<double, 3, 1>& dT, Eigen::MatrixXd& residuals);
  void ComputeResidualJacobians(std::vector<Eigen::Matrix<double, 3, 3> >& vA, std::vector<Eigen::Matrix<double, 3, 1> >& vX,
                                std::vector<Eigen::Matrix<double, 3, 1> >& vP, std::vector<double> vS,
                                Eigen::Matrix<double, 6, 1>& T, Eigen::MatrixXd& residualsJacobians);

  // Instead of taking the k-nearest neigbirs in the odometry
  // step we will take specific neighbor using the particularities
  // of the velodyne's lidar sensor
  void GetEgoMotionLineSpecificNeighbor(std::vector<int>& nearestValid, std::vector<float>& nearestValidDist,
                                        unsigned int nearestSearch, pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousEdges, Point p);
  void GetEgoMotionPlaneSpecificNeighbor();

  // Instead of taking the k-nearest neighbors in the mapping
  // step we will take specific neighbor using a sample consensus
  // model
  void GetMappingLineSpecificNeigbbor(std::vector<int>& nearestValid, std::vector<float>& nearestValidDist, double maxDistInlier,
                                        unsigned int nearestSearch, pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousEdges, Point p);
  void GetMappingPlaneSpecificNeigbbor();

  // Express the provided point into the referential of the sensor
  // at time t0. The referential at time of acquisition t is estimated
  // using the constant velocity hypothesis and the provided sensor
  // position estimation
  void ExpressPointInStartReferencial(Point& p, vtkSmartPointer<vtkVelodyneTransformInterpolator> undistortionInterp);

  // Express the keypoints into the referential of the sensor
  // at time t1. The referential at time of acquisition t is estimated
  // using the constant velocity hypothesis and the provided sensor
  // position estimation
  void ExpressKeypointsInEndFrameRef();
  void ExpressPointInEndReferencial(Point& p, vtkSmartPointer<vtkVelodyneTransformInterpolator> undistortionInterp);

  // Initialize the undistortion interpolator
  vtkSmartPointer<vtkVelodyneTransformInterpolator> InitUndistortionInterpolator();

  // Update the world transformation by integrating
  // the relative motion recover and the previous
  // world transformation
  void UpdateTworldUsingTrelative();

  // Initialize Tworld using external data provided
  // by an external sensor (GPS / IMU, ...)
  void InitTworldUsingExternalData(double adjustedTime0, double rawTime0);

  // Fill the information array with default value
  // it is used if a mapping step is skipped for example
  void FillMappingInfoArrayWithDefaultValues();
  void FillEgoMotionInfoArrayWithDefaultValues();

  // Predict Tworld using last points of the trajectory
  Eigen::Matrix<double, 6, 1> PredictTWorld();

  // Update the maps by populate the rolling grids
  // using the current keypoints expressed in the
  // world reference frame coordinate system
  void UpdateMapsUsingTworld();

  // To recover the ego-motion we have to minimize the function
  // f(R, T) = sum(d(point, line)^2) + sum(d(point, plane)^2). In both
  // case the distance between the point and the line / plane can be
  // writen (R*X+T - P).t * A * (R*X+T - P). Where X is the key point
  // P is a point on the line / plane. A = (n*n.t) for a plane with n
  // being the normal and A = (I - n*n.t)^2 for a line with n being
  // a director vector of the line
  // - Avalues will store the A matrix
  // - Pvalues will store the P points
  // - Xvalues will store the W points
  // - OutlierDistScale will attenuate the distance function for outliers
  // - TimeValues store the time acquisition
  std::vector<Eigen::Matrix<double, 3, 3> > Avalues;
  std::vector<Eigen::Matrix<double, 3, 1> > Pvalues;
  std::vector<Eigen::Matrix<double, 3, 1> > Xvalues;
  std::vector<double> RadiusIncertitude;
  std::vector<double> OutlierDistScale;
  std::vector<double> TimeValues;
  void ResetDistanceParameters();


  // Display infos
  template<typename T, typename Tvtk>
  void AddVectorToPolydataPoints(const std::vector<std::vector<T>>& vec, const char* name, vtkPolyData* pd);
  void DisplayLaserIdMapping(vtkSmartPointer<vtkPolyData> input);
  void DisplayRelAdv(vtkSmartPointer<vtkPolyData> input);

  // Indicate if we are in display mode or not
  // Display mode will add arrays showing some
  // results of the slam algorithm such as
  // the keypoints extracted, curvature etc
  bool DisplayMode;

  // Identity matrix
  Eigen::Matrix<double, 3, 3> I3;
  Eigen::Matrix<double, 6, 6> I6;
};

#endif // VTK_SLAM_H
