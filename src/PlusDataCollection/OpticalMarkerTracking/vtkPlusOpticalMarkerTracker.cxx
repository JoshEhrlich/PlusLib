/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#define MM_PER_M 1000

// Local includes
#include "PixelCodec.h"
#include "PlusConfigure.h"
#include "PlusVideoFrame.h"
#include "vtkPlusDataSource.h"
#include "vtkPlusOpticalMarkerTracker.h"

// VTK includes
#include <vtkExtractVOI.h>
#include <vtkImageData.h>
#include <vtkImageImport.h>
#include <vtkMath.h>
#include <vtkMatrix4x4.h>
#include <vtkObjectFactory.h>

// OS includes
#include <fstream>
#include <iostream>
#include <set>

// RANSAC includes
#include "RANSAC.h"
#include "ParametersEstimator.h"
#include "PlaneParametersEstimator.h"

// VNL includes
#include <vnl_cross.h>
#include <vnl_matrix.h>
#include <vnl_vector.h>

// aruco includes
#include <markerdetector.h>
#include <cameraparameters.h>
#include <dictionary.h>
#include <posetracker.h>

// OpenCV includes
#include <opencv2/highgui.hpp>
#include <opencv2/calib3d.hpp>

//TODO: for testing
#include "vtkXMLPolyDataReader.h"
#include "vtkPolyDataMapper.h"
#include "vtkActor.h"
#include "vtkRenderer.h"
#include "vtkRenderWindow.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkAxesActor.h"
#include "vtkOrientationMarkerWidget.h"
// are you really for testing?
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"

//TODO: Video and polydata indices should be set based on the input channels.
// TODO: clean this up... shouldn't have global vars (move them into their respective methods)

static const int LEFT_BOUNDARY = false;
static const int RIGHT_BOUNDARY = true;
static const double PI = 3.14159265358979323846;

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPlusOpticalMarkerTracker);

namespace
{
  class TrackedTool
  {
  public:
    enum TOOL_MARKER_TYPE
    {
      SINGLE_MARKER,
      MARKER_MAP
    };

    // Defines the method of data fusion.
    enum DATA_FUSION_METHOD
    {
      FUSION_RGB_ONLY,
      FUSION_DEPTH_ONLY,
      FUSION_COMPONENT,
      FUSION_KALMAN
    };

    TrackedTool(int markerId, float markerSizeMm, const std::string& toolSourceId, DATA_FUSION_METHOD fusionMethod)
      : ToolMarkerType(SINGLE_MARKER)
      , MarkerId(markerId)
      , MarkerSizeMm(markerSizeMm)
      , ToolSourceId(toolSourceId)
      , DataFusionMethod(fusionMethod)
    {
    }
    TrackedTool(const std::string& markerMapFile, const std::string& toolSourceId, DATA_FUSION_METHOD fusionMethod)
      : ToolMarkerType(MARKER_MAP)
      , MarkerMapFile(markerMapFile)
      , ToolSourceId(toolSourceId)
      , DataFusionMethod(fusionMethod)
    {
    }

    int MarkerId;
    TOOL_MARKER_TYPE ToolMarkerType;
    float MarkerSizeMm;

    DATA_FUSION_METHOD DataFusionMethod;
    std::string MarkerMapFile;
    std::string ToolSourceId;
    std::string ToolName;
    aruco::MarkerPoseTracker MarkerPoseTracker;
    vtkSmartPointer<vtkMatrix4x4> RgbMarkerToCamera = vtkSmartPointer<vtkMatrix4x4>::New();
    vtkSmartPointer<vtkMatrix4x4> DepthMarkerToCamera = vtkSmartPointer<vtkMatrix4x4>::New();
    // previous result transform computed using DataFusionMethod, unused for FUSION_RGB_ONLY and FUSION_DEPTH_ONLY
    vtkSmartPointer<vtkMatrix4x4> PreviousMarkerToCamera = vtkSmartPointer<vtkMatrix4x4>::New();
  };
}

//----------------------------------------------------------------------------
class vtkPlusOpticalMarkerTracker::vtkInternal
{
public:
  vtkPlusOpticalMarkerTracker* External;

  vtkInternal(vtkPlusOpticalMarkerTracker* external)
    : External(external)
    , MarkerDetector(std::make_shared<aruco::MarkerDetector>())
    , CameraParameters(std::make_shared<aruco::CameraParameters>())
  {
  }

  virtual ~vtkInternal()
  {
    MarkerDetector = nullptr;
    CameraParameters = nullptr;
  }

  /*
   * Builds optical transform out of aruco pose tracking data
   */
  PlusStatus BuildOpticalTransformMatrix(
    vtkSmartPointer<vtkMatrix4x4> transformMatrix,
    const cv::Mat& Rvec,
    const cv::Mat& Tvec,
    cv::Mat& Rmat);

  //TODO: this should have PlusStatus return type
  void ComputePlaneTransform(
    vtkSmartPointer<vtkMatrix4x4> depthTransform,
    double x_axis[],
    double z_axis[],
    double center[]);

  /*
   * Computes the angle between two vectors.
   */
  // TODO: is there a vnl method for this, can I make this more generic?
  float VectorAngleDeg(vnl_vector<double> xAxis, vnl_vector<double> zAxis);

  /*
   * Computes the slope of the line x=my+b between corners 1 & 2.
   * If corenrs have the same x or y values then returns special value 0.0.
   */
  float DetermineSlope(cv::Point2d corner1, cv::Point2d corner2);

  /*
   * Determines if the marker is ALIGNED, SKEW_LEFT, SKEW_RIGHT or ROTATED
   * with respect to the image frame.  Re-orders corners so position 0 is top corner.
   */
  MARKER_ORIENTATION DetermineMarkerOrientation(std::vector<cv::Point2d>& corners);

  //TODO: rename all the Generate methods to Extract

  /*
   * Computes a boundary of the marker whose path is defined by corners.
   */
  void GenerateBoundary(int* boundary, std::vector<cv::Point2d> corners, int top, bool isRight);

  /*
   *
   */
  void GenerateRotatedItkData(
    vtkSmartPointer<vtkPolyData> vtkDepthData,
    std::vector<itk::Point<double, 3>> &itkData,
    std::vector<cv::Point2d> corners,
    /*for testing*/
    FrameSizeType dim,
    cv::Mat image
  );

  /*
   *
   */
  void GenerateSkewLeftItkData(
    vtkSmartPointer<vtkPolyData> vtkDepthData,
    std::vector<itk::Point<double, 3>> &itkData,
    std::vector<cv::Point2d> corners,
    /*for testing*/
    FrameSizeType dim,
    cv::Mat image
  );

  /*
   *
   */
  void GenerateSkewRightItkData(
    vtkSmartPointer<vtkPolyData> vtkDepthData,
    std::vector<itk::Point<double, 3>> &itkData,
    std::vector<cv::Point2d> corners,
    /*for testing*/
    FrameSizeType dim,
    cv::Mat image
  );

  /*
   * Copy marker plane from vtkPolyData into itk datastructure for RANSAC input.
   */
  void CopyToItkData(
    vtkSmartPointer<vtkPolyData> vtkDepthData,
    std::vector<itk::Point<double, 3>> &itkData,
    int top,
    int bottom,
    int *leftBoundary,
    int *rightBoundary);

  /*
   *
   */
  void GenerateItkData(
    vtkSmartPointer<vtkPolyData> vtkDepthData,
    std::vector<itk::Point<double, 3>> &itkData,
    std::vector<cv::Point2d> corners,
    /*for testing*/
    FrameSizeType dim,
    cv::Mat image
  );

  /*
   *
   */
  void ComputeComponentFusion(
    vtkSmartPointer<vtkMatrix4x4> RgbMarkerToCamera,
    vtkSmartPointer<vtkMatrix4x4> DepthMarkerToCamera,
    vtkSmartPointer<vtkMatrix4x4> PreviousMarkerToCamera
  );

  /*
  *
  */
  void ComputeKalmanFusion(
    vtkSmartPointer<vtkMatrix4x4> RgbMarkerToCamera,
    vtkSmartPointer<vtkMatrix4x4> DepthMarkerToCamera,
    vtkSmartPointer<vtkMatrix4x4> PreviousMarkerToCamera
  );

  // TODO: offload all depth plane fitting from InternalUpdate to DepthPlaneFit
  //PlusStatus DepthPlaneFit()

  std::string               CameraCalibrationFile;
  INPUT_TYPE                InputType;
  std::string               MarkerDictionary;
  std::vector<TrackedTool>  Tools;

  /*! Pointer to main aruco objects */
  std::shared_ptr<aruco::MarkerDetector>    MarkerDetector;
  std::shared_ptr<aruco::CameraParameters>  CameraParameters;
  std::vector<aruco::Marker>                Markers;
};

//----------------------------------------------------------------------------
vtkPlusOpticalMarkerTracker::vtkPlusOpticalMarkerTracker()
  : vtkPlusDevice()
  , Internal(new vtkInternal(this))
{
  this->FrameNumber = 0;
  this->StartThreadForInternalUpdates = true;
}

//----------------------------------------------------------------------------
vtkPlusOpticalMarkerTracker::~vtkPlusOpticalMarkerTracker()
{
  delete Internal;
  Internal = nullptr;
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}


//----------------------------------------------------------------------------
PlusStatus vtkPlusOpticalMarkerTracker::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  // TODO: Improve error checking
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  XML_READ_STRING_ATTRIBUTE_NONMEMBER_REQUIRED(CameraCalibrationFile, this->Internal->CameraCalibrationFile, deviceConfig);
  XML_READ_ENUM2_ATTRIBUTE_NONMEMBER_REQUIRED(InputType, this->Internal->InputType, deviceConfig, "RGB_ONLY", INPUT_RGB_ONLY, "RGB_AND_DEPTH", INPUT_RGB_AND_DEPTH);
  // TODO: Check correct number of input bulk data channels (video / vtkPolydata)

  XML_READ_STRING_ATTRIBUTE_NONMEMBER_REQUIRED(MarkerDictionary, this->Internal->MarkerDictionary, deviceConfig);

  XML_FIND_NESTED_ELEMENT_REQUIRED(dataSourcesElement, deviceConfig, "DataSources");
  for (int nestedElementIndex = 0; nestedElementIndex < dataSourcesElement->GetNumberOfNestedElements(); nestedElementIndex++)
  {
    vtkXMLDataElement* toolDataElement = dataSourcesElement->GetNestedElement(nestedElementIndex);
    if (STRCASECMP(toolDataElement->GetName(), "DataSource") != 0)
    {
      // if this is not a data source element, skip it
      continue;
    }
    if (toolDataElement->GetAttribute("Type") != NULL && STRCASECMP(toolDataElement->GetAttribute("Type"), "Tool") != 0)
    {
      // if this is not a Tool element, skip it
      continue;
    }

    const char* toolId = toolDataElement->GetAttribute("Id");
    if (toolId == NULL)
    {
      // tool doesn't have ID needed to generate transform
      LOG_ERROR("Failed to initialize OpticalMarkerTracking tool: DataSource Id is missing");
      continue;
    }

    PlusTransformName toolTransformName(toolId, this->GetToolReferenceFrameName());
    std::string toolSourceId = toolTransformName.GetTransformName();

    TrackedTool::DATA_FUSION_METHOD fusionMethod = TrackedTool::FUSION_RGB_ONLY;
    XML_READ_ENUM4_ATTRIBUTE_NONMEMBER_OPTIONAL(DataFusionMethod, fusionMethod, toolDataElement, "RGB_ONLY", TrackedTool::FUSION_RGB_ONLY, "DEPTH_ONLY", TrackedTool::FUSION_DEPTH_ONLY, "COMPONENT", TrackedTool::FUSION_COMPONENT, "KALMAN", TrackedTool::FUSION_KALMAN);

    if (this->Internal->InputType == INPUT_RGB_ONLY)
    {
      if (fusionMethod == TrackedTool::FUSION_DEPTH_ONLY)
      {
        LOG_ERROR("Tracked tool '" << toolId << "' is requesting 'DEPTH_ONLY' data fusion but depth data is not provided to OpticalMarkerTracker. Please provide depth data and set InputType='RGB_AND_DEPTH' or use DataFusionMethod='RGB_ONLY'.");
        return PLUS_FAIL;
      }
      else if (fusionMethod == TrackedTool::FUSION_COMPONENT)
      {
        LOG_ERROR("Tracked tool '" << toolId << "' is requesting 'COMPONENTS' data fusion but depth data is not provided to OpticalMarkerTracker. Please provide depth data and set InputType='RGB_AND_DEPTH' or use DataFusionMethod='RGB_ONLY'.");
        return PLUS_FAIL;
      }
      else if (fusionMethod == TrackedTool::FUSION_KALMAN)
      {
        LOG_ERROR("Tracked tool '" << toolId << "' is requesting 'KALMAN' data fusion but depth data is not provided to OpticalMarkerTracker. Please provide depth data and set InputType='RGB_AND_DEPTH' or use DataFusionMethod='RGB_ONLY'.");
        return PLUS_FAIL;
      }
    }

    // TODO: Check if both rgb and depth provided. If not, allow only FUSION_RGB_ONLY as DataFusionMethod.

    if (toolDataElement->GetAttribute("MarkerId") != NULL && toolDataElement->GetAttribute("MarkerSizeMm") != NULL)
    {
      // this tool is tracked by a single marker
      int MarkerId;
      toolDataElement->GetScalarAttribute("MarkerId", MarkerId);
      float MarkerSizeMm;
      toolDataElement->GetScalarAttribute("MarkerSizeMm", MarkerSizeMm);
      TrackedTool newTool(MarkerId, MarkerSizeMm, toolSourceId, fusionMethod);
      this->Internal->Tools.push_back(newTool);
    }
    else if (toolDataElement->GetAttribute("MarkerMapFile") != NULL)
    {
      // this tool is tracked by a marker map
      // TODO: Implement marker map tracking.
    }
    else
    {
      LOG_ERROR("Incorrectly formatted tool data source.");
    }
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusOpticalMarkerTracker::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(deviceConfig, rootConfigElement);

  if (!this->Internal->CameraCalibrationFile.empty())
  {
    deviceConfig->SetAttribute("CameraCalibrationFile", this->Internal->CameraCalibrationFile.c_str());
  }
  if (!this->Internal->MarkerDictionary.empty())
  {
    deviceConfig->SetAttribute("MarkerDictionary", this->Internal->MarkerDictionary.c_str());
  }
  switch (this->Internal->InputType)
  {
  case INPUT_RGB_ONLY:
    deviceConfig->SetAttribute("TrackingMethod", "RGB");
    break;
  case INPUT_RGB_AND_DEPTH:
    deviceConfig->SetAttribute("TrackingMethod", "RGB_AND_DEPTH");
    break;
  default:
    LOG_ERROR("Unknown tracking method passed to vtkPlusOpticalMarkerTracker::WriteConfiguration");
    return PLUS_FAIL;
  }

  //TODO: Write data for custom attributes

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusOpticalMarkerTracker::Probe()
{
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusOpticalMarkerTracker::InternalConnect()
{
  // get calibration file path && check file exists
  std::string calibFilePath = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(this->Internal->CameraCalibrationFile);
  LOG_INFO("Use aruco camera calibration file located at: " << calibFilePath);
  if (!vtksys::SystemTools::FileExists(calibFilePath.c_str(), true))
  {
    LOG_ERROR("Unable to find aruco camera calibration file at: " << calibFilePath);
    return PLUS_FAIL;
  }

  // TODO: Need error handling for this?
  this->Internal->CameraParameters->readFromXMLFile(calibFilePath);
  this->Internal->MarkerDetector->setDictionary(this->Internal->MarkerDictionary);
  // threshold tuning numbers from aruco_test
  this->Internal->MarkerDetector->setThresholdParams(7, 7);
  this->Internal->MarkerDetector->setThresholdParamRange(2, 0);

  bool lowestRateKnown = false;
  double lowestRate = 30; // just a usual value (FPS)
  for (ChannelContainerConstIterator it = begin(this->InputChannels); it != end(this->InputChannels); ++it)
  {
    vtkPlusChannel* anInputStream = (*it);
    if (anInputStream->GetOwnerDevice()->GetAcquisitionRate() < lowestRate || !lowestRateKnown)
    {
      lowestRate = anInputStream->GetOwnerDevice()->GetAcquisitionRate();
      lowestRateKnown = true;
    }
  }
  if (lowestRateKnown)
  {
    this->AcquisitionRate = lowestRate;
  }
  else
  {
    LOG_WARNING("vtkPlusOpticalMarkerTracker acquisition rate is not known");
  }

  this->LastProcessedInputDataTimestamp = 0;
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusOpticalMarkerTracker::InternalDisconnect()
{
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusOpticalMarkerTracker::InternalStartRecording()
{
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusOpticalMarkerTracker::InternalStopRecording()
{
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusOpticalMarkerTracker::vtkInternal::BuildOpticalTransformMatrix(
  vtkSmartPointer<vtkMatrix4x4> transformMatrix,
  const cv::Mat& Rvec,
  const cv::Mat& Tvec,
  cv::Mat& Rmat)
{
  transformMatrix->Identity();
  try
  {
    cv::Rodrigues(Rvec, Rmat);
  }
  catch (...)
  {
    return PLUS_FAIL;
  }

  for (int x = 0; x <= 2; x++)
  {
    transformMatrix->SetElement(x, 3, MM_PER_M * Tvec.at<float>(x, 0));
    for (int y = 0; y <= 2; y++)
    {
      transformMatrix->SetElement(x, y, Rmat.at<float>(x, y));
    }
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::vtkInternal::ComputePlaneTransform(
  vtkSmartPointer<vtkMatrix4x4> MarkerToDepthCamera,
  double x_axis[],
  double z_axis[],
  double center[])
{
  // TODO: add homogenous coordinate to all raw array vectors
  double z_expected[4] = { 0, 0, -1, 0 };
  z_axis[1] = -z_axis[1]; // left-handed to right-handed coord sys conversion
  float ZtoZAngle = vtkMath::Dot(z_expected, z_axis);
  LOG_INFO("cos(ZtoZangle): " << ZtoZAngle);
  if (ZtoZAngle < 0)
  {
    // normal is pointing towards back of marker, flip it, as below
    z_axis[0] *= -1;
    z_axis[1] *= -1;
    z_axis[2] *= -1;
    z_axis[3] *= -1; // does the homogenous coordinate need to be flipped?
  }
  vnl_vector<double> xGuess(3, 3, x_axis);
  vnl_vector<double> zAxis(3, 3, z_axis);
  xGuess.normalize();
  zAxis.normalize();

  double x_theoretical[3] = { 1, 0, 0 };
  vnl_vector<double> xTheoretical(3, 3, x_theoretical);
  double y_theoretical[3] = { 0, 1, 0 };
  vnl_vector<double> yTheoretical(3, 3, y_theoretical);

  vnl_vector<double> yAxis;
  vnl_vector<double> xAxis;

  LOG_ERROR(VectorAngleDeg(xGuess, zAxis));
  if (VectorAngleDeg(xGuess, zAxis) > 10)
  {
    LOG_ERROR("Using x_axis from aruco");
    yAxis = vnl_cross_3d(zAxis, xGuess);
    xAxis = vnl_cross_3d(yAxis, zAxis);
  }
  else if (VectorAngleDeg(xTheoretical, zAxis) > 10)
  {
    LOG_ERROR("using theoretical x_axis");
    yAxis = vnl_cross_3d(zAxis, xTheoretical);
    xAxis = vnl_cross_3d(yAxis, zAxis);
  }
  else
  {
    LOG_ERROR("Using theoretical y_axis as perpendicular to Z");
    xAxis = vnl_cross_3d(yTheoretical, zAxis);
    yAxis = vnl_cross_3d(zAxis, xAxis);
  }

  vnl_matrix<double> Rotation(3, 3);
  Rotation.set_column(0, xAxis);
  Rotation.set_column(1, yAxis);
  Rotation.set_column(2, zAxis);

  MarkerToDepthCamera->Identity();
  for (int row = 0; row <= 2; row++)
  {
    for (int col = 0; col <= 2; col++)
      MarkerToDepthCamera->SetElement(row, col, Rotation(row, col));
  }
  MarkerToDepthCamera->SetElement(0, 3, center[0]);
  MarkerToDepthCamera->SetElement(1, 3, -center[1]);
  MarkerToDepthCamera->SetElement(2, 3, center[2]);

  //TODO: implement rotation conversion to standard tracker axes
}

//----------------------------------------------------------------------------
float vtkPlusOpticalMarkerTracker::vtkInternal::VectorAngleDeg(vnl_vector<double> xAxis, vnl_vector<double> zAxis)
{
  float dotProduct = xAxis(0)*zAxis(0) + xAxis(1)*zAxis(1) + xAxis(2)*zAxis(2);
  return abs(acos(dotProduct) * 180 / PI);
}

//----------------------------------------------------------------------------
float vtkPlusOpticalMarkerTracker::vtkInternal::DetermineSlope(cv::Point2d corner1, cv::Point2d corner2)
{
  if (corner1.y == corner2.y)
  {
    return 0.0;
  }
  else
  {
    return ((float)(corner1.x - corner2.x)) / (corner1.y - corner2.y);
  }
}

//----------------------------------------------------------------------------
vtkPlusOpticalMarkerTracker::MARKER_ORIENTATION vtkPlusOpticalMarkerTracker::vtkInternal::DetermineMarkerOrientation(std::vector<cv::Point2d>& corners)
{
  double yMin = corners[0].y;
  int top = 0;
  for (int i = 1; i < 4; i++)
  {
    if (corners[i].y < yMin)
    {
      yMin = corners[i].y;
      top = i;
    }
  }

  std::vector<cv::Point2d> orderedCorners;
  // set vertices in clockwise order (top = 0, ...)
  for (int i = 0; i < 4; i++) {
    orderedCorners.push_back(corners[(top + i) % 4]);
  }

  corners = orderedCorners;

  // find index of bottom corner
  int yMax = corners[0].y;
  int bottom = 0;
  for (int i = 1; i < 4; i++)
  {
    if (corners[i].y > yMax)
    {
      yMax = corners[i].y;
      bottom = i;
    }
  }

  switch (bottom)
  {
  case 1:
    LOG_INFO("SKEW_LEFT");
    return SKEW_LEFT;
  case 2:
    LOG_INFO("ROTATED");
    return ROTATED;
  default:
    LOG_INFO("SKEW_RIGHT");
    return SKEW_RIGHT;
  }
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::vtkInternal::GenerateBoundary(int* boundary, std::vector<cv::Point2d> corners, int top, bool isRight)
{
  int numSegments = corners.size() - 1;

  for (int segIndex = 0; segIndex < numSegments; segIndex++)
  {
    int segTop = corners[segIndex].y;
    int segBottom = corners[segIndex + 1].y;
    float mPx = DetermineSlope(corners[segIndex], corners[segIndex + 1]);
    int x1Px = corners[segIndex].x;
    int y1Px = corners[segIndex].y;
    for (int yPx = segTop; yPx <= segBottom; yPx++)
    {
      boundary[yPx - top] = mPx * (yPx - y1Px) + x1Px;
    }
  }
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::vtkInternal::GenerateRotatedItkData(
  vtkSmartPointer<vtkPolyData> vtkDepthData,
  std::vector<itk::Point<double, 3>> &itkData,
  std::vector<cv::Point2d> corners,
  /*for testing*/
  FrameSizeType dim,
  cv::Mat image)
{
  const char TOP = 0, RIGHT = 1, BOTTOM = 2, LEFT = 3;
  int top = corners[TOP].y;
  int bottom = corners[BOTTOM].y;
  int height = bottom - top + 1;

  //LOG_WARNING("TOP    x:" << corners[TOP].x << " y: " << corners[TOP].y);
  //LOG_WARNING("BOTTOM x:" << corners[BOTTOM].x << " y: " << corners[BOTTOM].y);
  //LOG_WARNING("LEFT   x:" << corners[LEFT].x << " y: " << corners[LEFT].y);
  //LOG_WARNING("RIGHT  x:" << corners[RIGHT].x << " y: " << corners[RIGHT].y);
  //LOG_WARNING("height: " << height);

  // generate left boundary
  int* leftBoundary = new int[height];
  std::vector<cv::Point2d> leftPath;
  leftPath.push_back(corners[TOP]);
  leftPath.push_back(corners[LEFT]);
  leftPath.push_back(corners[BOTTOM]);
  GenerateBoundary(leftBoundary, leftPath, top, false);

  // generate right boundary
  int* rightBoundary = new int[height];
  std::vector<cv::Point2d> rightPath;
  rightPath.push_back(corners[TOP]);
  rightPath.push_back(corners[RIGHT]);
  rightPath.push_back(corners[BOTTOM]);
  GenerateBoundary(rightBoundary, rightPath, top, true);

  // copy vtk->itk
  CopyToItkData(vtkDepthData, itkData, top, bottom, leftBoundary, rightBoundary);
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::vtkInternal::GenerateSkewLeftItkData(
  vtkSmartPointer<vtkPolyData> vtkDepthData,
  std::vector<itk::Point<double, 3>> &itkData,
  std::vector<cv::Point2d> corners,
  /*for testing*/
  FrameSizeType dim,
  cv::Mat image)
{
  const char TOP = 0, BOTTOM = 1, LOWER_LEFT = 2, UPPER_LEFT = 3;
  int top = corners[TOP].y, bottom = corners[BOTTOM].y;
  int height = bottom - top;

  //LOG_WARNING("TOP        " << corners[TOP].y);
  //LOG_WARNING("BOTTOM     " << corners[BOTTOM].y);
  //LOG_WARNING("UPPER LEFT " << corners[UPPER_LEFT].y);
  //LOG_WARNING("LOWER LEFT " << corners[LOWER_LEFT].y);

  // generate left boundary
  int* leftBoundary = new int[height];
  std::vector<cv::Point2d> leftPath;
  leftPath.push_back(corners[TOP]);
  leftPath.push_back(corners[UPPER_LEFT]);
  leftPath.push_back(corners[LOWER_LEFT]);
  leftPath.push_back(corners[BOTTOM]);
  GenerateBoundary(leftBoundary, leftPath, top, LEFT_BOUNDARY);

  // generate right boundary
  int* rightBoundary = new int[height];
  std::vector<cv::Point2d> rightPath;
  rightPath.push_back(corners[TOP]);
  rightPath.push_back(corners[BOTTOM]);
  GenerateBoundary(rightBoundary, rightPath, top, RIGHT_BOUNDARY);

  // copy vtk->itk
  CopyToItkData(vtkDepthData, itkData, top, bottom, leftBoundary, rightBoundary);
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::vtkInternal::GenerateSkewRightItkData(
  vtkSmartPointer<vtkPolyData> vtkDepthData,
  std::vector<itk::Point<double, 3>> &itkData,
  std::vector<cv::Point2d> corners,
  /*for testing*/
  FrameSizeType dim,
  cv::Mat image)
{
  const char TOP = 0, UPPER_RIGHT = 1, LOWER_RIGHT = 2, BOTTOM = 3;
  int top = corners[TOP].y;
  int bottom = corners[BOTTOM].y;
  int height = bottom - top + 1;

  //LOG_WARNING("TOP          x: " << corners[TOP].x << " y: " << corners[TOP].y);
  //LOG_WARNING("BOTTOM       X: " << corners[BOTTOM].x << " y: " << corners[BOTTOM].y);
  //LOG_WARNING("UPPER RIGHT  x: " << corners[UPPER_RIGHT].x << " y: " << corners[UPPER_RIGHT].y);
  //LOG_WARNING("LOWER RIGHT  x: " << corners[LOWER_RIGHT].x << " y: " << corners[LOWER_RIGHT].y);

  // generate left boundary
  int* leftBoundary = new int[height];
  std::vector<cv::Point2d> leftPath;
  leftPath.push_back(corners[TOP]);
  leftPath.push_back(corners[BOTTOM]);
  GenerateBoundary(leftBoundary, leftPath, top, LEFT_BOUNDARY);

  // generate right boundary
  int* rightBoundary = new int[height];
  std::vector<cv::Point2d> rightPath;
  rightPath.push_back(corners[TOP]);
  rightPath.push_back(corners[UPPER_RIGHT]);
  rightPath.push_back(corners[LOWER_RIGHT]);
  rightPath.push_back(corners[BOTTOM]);
  GenerateBoundary(rightBoundary, rightPath, top, RIGHT_BOUNDARY);

  // copy vtk->itk
  CopyToItkData(vtkDepthData, itkData, top, bottom, leftBoundary, rightBoundary);
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::vtkInternal::CopyToItkData(
  vtkSmartPointer<vtkPolyData> vtkDepthData,
  std::vector<itk::Point<double, 3>> &itkData,
  int top,
  int bottom,
  int *leftBoundary,
  int *rightBoundary)
{
  itk::Point<double, 3> itkPoint;
  // for testing
  vtkSmartPointer<vtkPoints> points = vtkSmartPointer<vtkPoints>::New();
  vtkSmartPointer<vtkCellArray> vertices = vtkSmartPointer<vtkCellArray>::New();

  for (int yPx = top; yPx <= bottom; yPx++)
  {
    for (int xPx = leftBoundary[yPx - top]; xPx <= rightBoundary[yPx - top]; xPx++)
    { 
      // TODO: Use non hard-coded dimension
      vtkIdType ptId = 640 * yPx + xPx;
      double vtkPoint[3];
      vtkDepthData->GetPoint(ptId, vtkPoint);

      // depth filter to select only points between 5cm and 200cm
      if (vtkPoint[2] > 50 && vtkPoint[2] < 2000)
      {
        itkPoint[0] = vtkPoint[0];
        itkPoint[1] = vtkPoint[1];
        itkPoint[2] = vtkPoint[2];
        itkData.push_back(itkPoint);

        //LOG_WARNING("x: " << xPx << " y: " << yPx);
        //TODO: use non hard-coded dimensions here
        vtkIdType pid[1];
        pid[0] = points->InsertNextPoint(vtkPoint[0], vtkPoint[1], vtkPoint[2]);
        vertices->InsertNextCell(1, pid);
      }
    }
  }

  if (false)
  {
    vtkSmartPointer<vtkPolyData> polyPlane = vtkSmartPointer<vtkPolyData>::New();
    polyPlane->SetPoints(points);
    polyPlane->SetVerts(vertices);

    // show polydata plane for testing
    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(polyPlane);
    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    vtkSmartPointer<vtkRenderer> renderer = vtkSmartPointer<vtkRenderer>::New();
    vtkSmartPointer<vtkRenderWindow> renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
    renderWindow->AddRenderer(renderer);
    vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
    renderWindowInteractor->SetRenderWindow(renderWindow);
    renderer->AddActor(actor);
    renderer->SetBackground(.2, .3, .4);
    vtkSmartPointer<vtkAxesActor> axes = vtkSmartPointer<vtkAxesActor>::New();
    vtkSmartPointer<vtkOrientationMarkerWidget> widget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    widget->SetOutlineColor(0.93, 0.57, 0.13);
    widget->SetOrientationMarker(axes);
    widget->SetInteractor(renderWindowInteractor);
    widget->SetViewport(0, 0, 0.4, 0.4);
    widget->SetEnabled(1);
    widget->InteractiveOn();
    renderer->ResetCamera();
    renderWindow->Render();
    renderWindowInteractor->Start();
    LOG_INFO("num points: " << polyPlane->GetNumberOfPoints());
  }
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::vtkInternal::GenerateItkData(
  vtkSmartPointer<vtkPolyData> vtkDepthData,
  std::vector<itk::Point<double, 3>> &itkData,
  std::vector<cv::Point2d> corners,
  /*for testing*/
  FrameSizeType dim,
  cv::Mat image)
{
  MARKER_ORIENTATION orientation = DetermineMarkerOrientation(corners);

  switch (orientation)
  {
  case SKEW_LEFT:
    GenerateSkewLeftItkData(vtkDepthData, itkData, corners, dim, image);
    return;
  case ROTATED:
    GenerateRotatedItkData(vtkDepthData, itkData, corners, dim, image);
    return;
  case SKEW_RIGHT:
    GenerateSkewRightItkData(vtkDepthData, itkData, corners, dim, image);
    return;
  }
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::vtkInternal::ComputeComponentFusion(
  vtkSmartPointer<vtkMatrix4x4> RgbMarkerToCamera,
  vtkSmartPointer<vtkMatrix4x4> DepthMarkerToCamera,
  vtkSmartPointer<vtkMatrix4x4> PreviousMarkerToCamera
)
{
  PreviousMarkerToCamera->Identity();
  // using rotation from RGB
  for (int row = 0; row < 3; row++)
  {
    for (int col = 0; col < 3; col++)
    {
      PreviousMarkerToCamera->SetElement(row, col, RgbMarkerToCamera->GetElement(row, col));
    }
  }
  // using rotation from Depth
  //for (int row = 0; row < 3; row++)
  //{
  //  for (int col = 0; col < 3; col++)
  //  {
  //    PreviousMarkerToCamera->SetElement(row, col, RgbMarkerToCamera->GetElement(row, col));
  //  }
  //}
  // x, y positions from Optical
  PreviousMarkerToCamera->SetElement(0, 3, RgbMarkerToCamera->GetElement(0, 3));
  PreviousMarkerToCamera->SetElement(1, 3, RgbMarkerToCamera->GetElement(1, 3));
  // z position from depth
  PreviousMarkerToCamera->SetElement(2, 3, DepthMarkerToCamera->GetElement(2, 3));
}

//----------------------------------------------------------------------------
void vtkPlusOpticalMarkerTracker::vtkInternal::ComputeKalmanFusion(
  vtkSmartPointer<vtkMatrix4x4> RgbMarkerToCamera,
  vtkSmartPointer<vtkMatrix4x4> DepthMarkerToCamera,
  vtkSmartPointer<vtkMatrix4x4> PreviousMarkerToCamera
)
{

}

//----------------------------------------------------------------------------
PlusStatus vtkPlusOpticalMarkerTracker::InternalUpdate()
{
  static const int CHANNEL_INDEX_VIDEO = 0;
  static const int CHANNEL_INDEX_POLYDATA = 1;

  if (this->Internal->InputType == INPUT_RGB_ONLY)
  {
    if (this->InputChannels.size() != 1)
    {
      LOG_ERROR("OpticalMarkerTracker device requires exactly 1 input stream (that contains video data). Check configuration.");
      return PLUS_FAIL;
    }
  }
  else if (this->Internal->InputType = INPUT_RGB_AND_DEPTH)
  {
    if (this->InputChannels.size() != 2)
    {
      LOG_ERROR("OpticalMarkerTracker device requires exactly 2 input streams (that contain video data and depth data). Check configuration.");
      return PLUS_FAIL;
    }
  }

  // check if data is ready
  if (!this->InputChannels[CHANNEL_INDEX_VIDEO]->GetVideoDataAvailable())
  {
    LOG_TRACE("OpticalMarkerTracker is not tracking, video data is not available yet. Device ID: " << this->GetDeviceId());
    return PLUS_SUCCESS;
  }

  if (this->Internal->InputType == INPUT_RGB_AND_DEPTH && !this->InputChannels[CHANNEL_INDEX_POLYDATA]->GetBulkDataAvailable())
  {
    LOG_TRACE("OpticalMarkerTracker is not tracking, depth data is not available yet. Device ID: " << this->GetDeviceId());
    return PLUS_SUCCESS;
  }

  // get timestamp of frame to process from PolyData (as it is added to the buffers after video)
  double oldestTrackingTimestamp(0);
  if (this->Internal->InputType == INPUT_RGB_AND_DEPTH && this->InputChannels[CHANNEL_INDEX_POLYDATA]->GetLatestTimestamp(oldestTrackingTimestamp) == PLUS_SUCCESS)
  {
    if (this->LastProcessedInputDataTimestamp > oldestTrackingTimestamp)
    {
      LOG_INFO("Processed image generation started. No tracking data was available between " << this->LastProcessedInputDataTimestamp << "-" << oldestTrackingTimestamp <<
        "sec, therefore no processed images were generated during this time period.");
      this->LastProcessedInputDataTimestamp = oldestTrackingTimestamp;
    }
  }

  // grab tracked frames to process from buffer
  PlusTrackedFrame trackedVideoFrame;
  PlusTrackedFrame trackedPolyDataFrame;
  if (this->Internal->InputType == INPUT_RGB_ONLY || this->Internal->InputType == INPUT_RGB_AND_DEPTH)
  {
    // get optical video data
    if (this->InputChannels[CHANNEL_INDEX_VIDEO]->GetTrackedFrame(trackedVideoFrame) != PLUS_SUCCESS)
    {
      LOG_ERROR("Error while getting latest tracked frame. Last recorded timestamp: " << std::fixed << this->LastProcessedInputDataTimestamp << ". Device ID: " << this->GetDeviceId());
      this->LastProcessedInputDataTimestamp = vtkPlusAccurateTimer::GetSystemTime(); // forget about the past, try to add frames that are acquired from now on
      return PLUS_FAIL;
    }
  }
  if (this->Internal->InputType == INPUT_RGB_AND_DEPTH)
  {
    // get depth PolyData
    if (this->InputChannels[CHANNEL_INDEX_POLYDATA]->GetTrackedFrame(oldestTrackingTimestamp, trackedPolyDataFrame) != PLUS_SUCCESS)
    {
      LOG_ERROR("Error while getting latest tracked frame. Last recorded timestamp: " << std::fixed << this->LastProcessedInputDataTimestamp << ". Device ID: " << this->GetDeviceId());
      this->LastProcessedInputDataTimestamp = vtkPlusAccurateTimer::GetSystemTime(); // forget about the past, try to add frames that are acquired from now on
      return PLUS_FAIL;
    }
  }

  // to visualize polydata for testing purposes...
  if (false) {
    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(trackedPolyDataFrame.GetPolyData());
    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    vtkSmartPointer<vtkRenderer> renderer = vtkSmartPointer<vtkRenderer>::New();
    vtkSmartPointer<vtkRenderWindow> renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
    renderWindow->AddRenderer(renderer);
    vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
    renderWindowInteractor->SetRenderWindow(renderWindow);
    renderer->AddActor(actor);
    renderer->SetBackground(.2, .3, .4);
    vtkSmartPointer<vtkAxesActor> axes = vtkSmartPointer<vtkAxesActor>::New();
    vtkSmartPointer<vtkOrientationMarkerWidget> widget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    widget->SetOutlineColor(0.93, 0.57, 0.13);
    widget->SetOrientationMarker(axes);
    widget->SetInteractor(renderWindowInteractor);
    widget->SetViewport(0, 0, 0.4, 0.4);
    widget->SetEnabled(1);
    widget->InteractiveOn();
    renderer->ResetCamera();
    renderWindow->Render();
    renderWindowInteractor->Start();
  }

  // get frame dimensions & raw data
  FrameSizeType dim = trackedVideoFrame.GetFrameSize();
  PlusVideoFrame* frame = trackedVideoFrame.GetImageData();

  cv::Mat image(dim[1], dim[0], CV_8UC3);
  cv::Mat temp(dim[1], dim[0], CV_8UC3);

  //TODO: Flip image so that it's input to openCV in the correct orientation
  PlusVideoFrame *uprightOcvImage;
  PlusVideoFrame::FlipInfoType flip;

  // Plus image uses RGB and OpenCV uses BGR, swapping is only necessary for colored markers
  //PixelCodec::RgbBgrSwap(dim[0], dim[1], (unsigned char*)frame->GetScalarPointer(), temp.data);
  image.data = (unsigned char*)frame->GetScalarPointer();

  vtkSmartPointer<vtkPolyData> markerPolyData = trackedPolyDataFrame.GetPolyData();

  // detect markers in frame
  this->Internal->MarkerDetector->detect(image, this->Internal->Markers);
  const double unfilteredTimestamp = vtkPlusAccurateTimer::GetSystemTime();

  // iterate through tools computing RGB and Depth transforms for each tool
  // update each tool with the transform computed using the requested fusion method
  for (vector<TrackedTool>::iterator toolIt = begin(this->Internal->Tools); toolIt != end(this->Internal->Tools); ++toolIt)
  {
    bool toolInFrame = false;
    for (vector<aruco::Marker>::iterator markerIt = begin(this->Internal->Markers); markerIt != end(this->Internal->Markers); ++markerIt)
    {
      if (toolIt->MarkerId == markerIt->id) {
        //marker is in frame
        toolInFrame = true;

        // todo: make min error ratio a settable parameter in config
        if (toolIt->MarkerPoseTracker.estimatePose(*markerIt, *this->Internal->CameraParameters, toolIt->MarkerSizeMm / MM_PER_M, 4))
        {
          // UPDATE OPTICAL TRANSFORM
          cv::Mat Rvec = toolIt->MarkerPoseTracker.getRvec();
          cv::Mat Tvec = toolIt->MarkerPoseTracker.getTvec();
          cv::Mat Rmat(3, 3, CV_32FC1);
          this->Internal->BuildOpticalTransformMatrix(toolIt->RgbMarkerToCamera, Rvec, Tvec, Rmat);

          // todo: cache the results of optical transformation computation and depth computation calculation for each marker
          // todo: lazy evaluation, only evaluate depth if user requests FUSION_COMPONENT or FUSION_KALMAN
          if (this->Internal->InputType == INPUT_RGB_AND_DEPTH)
          {
            // UPDATE DEPTH TRANSFORM
            // get marker corners
            std::vector<cv::Point2d> corners;
            corners = markerIt->getCornersPx();

            // copy data from inside the marker into data structure for RANSAC plane algorithm
            std::vector<itk::Point<double, 3>> itkPlane;
            this->Internal->GenerateItkData(markerPolyData, itkPlane, corners, dim, image);

            // find plane normal and distance using RANSAC
            std::vector<double> ransacParameterResult;
            typedef itk::PlaneParametersEstimator<3> PlaneEstimatorType;
            typedef itk::RANSAC<itk::Point<double, 3>, double> RANSACType;

            //create and initialize the parameter estimator
            double maximalDistanceFromPlane = 0.5;
            PlaneEstimatorType::Pointer planeEstimator = PlaneEstimatorType::New();
            planeEstimator->SetDelta(maximalDistanceFromPlane);
            planeEstimator->LeastSquaresEstimate(itkPlane, ransacParameterResult);

            //create and initialize the RANSAC algorithm
            double desiredProbabilityForNoOutliers = 0.90;
            RANSACType::Pointer ransacEstimator = RANSACType::New();

            try
            {
              ransacEstimator->SetData(itkPlane);
            }
            catch (std::exception& e)
            {
              LOG_DEBUG(e.what());
              return PLUS_SUCCESS;
            }

            try
            {
              ransacEstimator->SetParametersEstimator(planeEstimator.GetPointer());
            }
            catch (std::exception& e)
            {
              LOG_DEBUG(e.what());
              return PLUS_SUCCESS;
            }

            // todo: RANSAC causes massive pauses in tracking... how do we make it faster?
            // using least squares for now

            /*try
            {
              ransacEstimator->Compute(ransacParameterResult, desiredProbabilityForNoOutliers);
            }
            catch (std::exception& e)
            {
              LOG_DEBUG(e.what());
              return PLUS_SUCCESS;
            }*/

            // print results of least squares / RANSAC plane fit
            if (ransacParameterResult.empty())
            {
              LOG_WARNING("Unable to fit line through points with least squares estimation");
              continue;
            }
            /*else
            {
              LOG_INFO("Least squares line parameters (n, a):");
              for (unsigned int i = 0; i < (2 * 3); i++)
              {
                LOG_INFO(" RANSAC parameter: " << ransacParameterResult[i]);
              }
            }*/

            double zAxis[4];
            zAxis[0] = ransacParameterResult[0];
            zAxis[1] = ransacParameterResult[1];
            zAxis[2] = ransacParameterResult[2];
            zAxis[3] = 0;

            double xAxis[4];
            xAxis[0] = Rmat.at<float>(0, 0);
            xAxis[1] = Rmat.at<float>(1, 0);
            xAxis[2] = Rmat.at<float>(2, 0);
            xAxis[3] = 0;

            // center is currently computed using the center of mass of the plane from least squares,
            double center[4];
            center[0] = ransacParameterResult[3];
            center[1] = ransacParameterResult[4];
            center[2] = ransacParameterResult[5];
            center[3] = 0;

            this->Internal->ComputePlaneTransform(toolIt->DepthMarkerToCamera, xAxis, zAxis, center);
          }
          
          if (toolIt->DataFusionMethod == TrackedTool::FUSION_RGB_ONLY)
          {
            // update tool transform with RGB only
            ToolTimeStampedUpdate(toolIt->ToolSourceId, toolIt->RgbMarkerToCamera, TOOL_OK, this->FrameNumber, unfilteredTimestamp);
            LOG_INFO("FUSION_RGB");
          }
          else if (toolIt->DataFusionMethod == TrackedTool::FUSION_DEPTH_ONLY)
          {
            // udpate tool transform with Depth only
            ToolTimeStampedUpdate(toolIt->ToolSourceId, toolIt->DepthMarkerToCamera, TOOL_OK, this->FrameNumber, unfilteredTimestamp);
            LOG_INFO("FUSION_DEPTH");
          }
          else if (toolIt->DataFusionMethod == TrackedTool::FUSION_COMPONENT)
          {
            // compute component fusion and update tool transform
            this->Internal->ComputeComponentFusion(toolIt->RgbMarkerToCamera, toolIt->DepthMarkerToCamera, toolIt->PreviousMarkerToCamera);
            ToolTimeStampedUpdate(toolIt->ToolSourceId, toolIt->PreviousMarkerToCamera, TOOL_OK, this->FrameNumber, unfilteredTimestamp);
            LOG_INFO("FUSION_COMPONENT");
          }
          else if (toolIt->DataFusionMethod == TrackedTool::FUSION_KALMAN)
          {
            // compute Kalman filter fusion and update tool transform
            LOG_INFO("FUSION_KALMAN");
          }
        }
        else
        {
          // pose estimation failed
          // TODO: add frame num, marker id, etc. Make this error more helpful.  Is there a way to handle it?
          LOG_ERROR("Pose estimation failed. Tool " << toolIt->ToolSourceId << " with marker " << toolIt->MarkerId << ".");
        }
        break;
      }
    }
    if (!toolInFrame) {
      // tool not in frame
      vtkSmartPointer<vtkMatrix4x4> identity = vtkSmartPointer<vtkMatrix4x4>::New();
      identity->Identity();
      ToolTimeStampedUpdate(toolIt->ToolSourceId, identity, TOOL_OUT_OF_VIEW, this->FrameNumber, unfilteredTimestamp);
    }
  }

  this->Modified();
  this->FrameNumber++;
  return PLUS_SUCCESS;
}