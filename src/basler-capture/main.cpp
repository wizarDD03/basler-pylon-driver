//
// Created by henry on 20/07/17.
//

//
// Created by anyone on 9/12/16.
//
#include <ros/ros.h>
#include <ros/package.h>

#include "pylon_parameters.hpp"
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/core/core.hpp"
#include <thread>         // std::thread
#include <pylon/PylonIncludes.h>
#include <pylon/usb/BaslerUsbInstantCamera.h>
typedef Pylon::CBaslerUsbInstantCamera Camera_t;
using namespace Basler_UsbCameraParams;

using namespace Pylon;
using namespace cv_bridge;

void getImage(Camera_t *camera, cv::Mat &mat_image){
//  CDeviceInfo deviceInfo = camera->GetDeviceInfo();

  CGrabResultPtr ptrGrabResult;
  CPylonImage image;
  CImageFormatConverter fc;
  fc.OutputPixelFormat = PixelType_BGR8packed;
  //Get the image from the camera's buffer
  camera->RetrieveResult( 5000, ptrGrabResult, TimeoutHandling_ThrowException);
//  camera->GrabOne()
  // Image grabbed successfully?
  if (ptrGrabResult->GrabSucceeded())
  {
    fc.Convert(image, ptrGrabResult);
    //Create a Mat structure for the image to be stored
    cv::Mat frame(ptrGrabResult->GetHeight(), ptrGrabResult->GetWidth(), CV_8UC3, (uint8_t *)image.GetBuffer());
    frame.copyTo(mat_image);
  } else{
    ROS_ERROR("Camera failed to grab!");
    exit(1);
  }
}

void setSlaveTrigger(Camera_t *camera) {
  try
  {
    //Set pin to fire camera/s
    camera->TriggerMode.SetValue(TriggerMode_On);
    camera->TriggerSource.SetValue(TriggerSource_Line3);

    //Set exposure
    camera->ExposureMode.SetValue(ExposureMode_Timed);
    //camera->ExposureTime.SetValue(400.0);

    //Set camera to start grabbing
    camera->StartGrabbing();
  }
  catch (const GenericException &e)
  {
    ROS_ERROR("Camera crashed! %s", e.GetDescription());

    exit(1);
  }
  return;
}

void setMasterTrigger(Camera_t *camera) {
  try
  {
    //Set exposure pin to go high when ready
    camera->LineSelector.SetValue(LineSelector_Line3);
    camera->LineMode.SetValue(LineMode_Output);
    camera->LineSource.SetValue(LineSource_ExposureActive);

    //Set software trigger to fire camera/s
    camera->AcquisitionMode.SetValue(AcquisitionMode_Continuous);
    camera->TriggerMode.SetValue(TriggerMode_On);
    camera->TriggerSource.SetValue(TriggerSource_Software);

    //Set exposure
    camera->ExposureMode.SetValue(ExposureMode_Timed);
    //camera->ExposureTime.SetValue(400.0);

    //Set camera to start grabbing
    camera->StartGrabbing();
  }
  catch (const GenericException &e)
  {
    ROS_ERROR("Camera crashed! %s", e.GetDescription());

    exit(1);
  }
  return;
}

void createCamera(Camera_t &curCamera, String_t camera_name){

  // Get the transport layer factory.
  CTlFactory& tlFactory = CTlFactory::GetInstance();

  // Get all attached devices and exit application if no device is found
  DeviceInfoList_t devices;
  tlFactory.EnumerateDevices(devices);

  //Create a camera instance for the camera matching the relevant serial number
  CDeviceInfo curCameraInfo;
  for (int i = 0; i < devices.size(); ++i) {
    if(!std::strcmp(devices[i].GetUserDefinedName(), camera_name)){
      ROS_INFO("Camera: %s", devices[i].GetUserDefinedName().c_str());
      curCameraInfo = devices[i];
    }
  }

  curCamera.Attach( CTlFactory::GetInstance().CreateDevice(curCameraInfo));
  curCamera.MaxNumBuffer = 1;
  curCamera.Open();
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "pylon_node");
  ros::NodeHandle nh;

  //GPIO
  //Pins
  //6 Brown DCon GND
  //5 Blue  Opto GND
  //4 White Line 2 : Opto
  //3 Red   Line 4 : Dcon Output Trigger
  //2 Black Line 1 : Opto
  //1 Green Line 3 : Dcon Input Trigger

  //Connect Green together with master Red
  //Connect Brown together for GND

  //camera
  //Command on line 2

  //camera and left_camera
  //Trigger input line 3

  //USB 3.0 GPIO pin out
  //Pin 5 is 12 o'clock with USB input at 6 o'clock, numbers then rotate clockwise

  ROS_INFO("Setup pub");
  image_transport::ImageTransport it(nh);
  image_transport::Publisher left_image_pub;
  image_transport::Publisher right_image_pub;

  std::string left_node;
  kiwibot::PylonParameters::getInstance()->getParameter(kiwibot::vision::LEFT_IMAGE_NODE_S, left_node);
  left_image_pub  = it.advertise(left_node, 1);

  std::string right_node;
  kiwibot::PylonParameters::getInstance()->getParameter(kiwibot::vision::RIGHT_IMAGE_NODE_S, right_node);
  right_image_pub = it.advertise(right_node, 1);

  // Before using any pylon methods, the pylon runtime must be initialized
  PylonInitialize();

  ROS_INFO("Opening cameras");
  Camera_t right_camera;
  Camera_t left_camera;

  //Create a Camera instance for each camera
  ROS_INFO("Right camera");
  std::string right;
  kiwibot::PylonParameters::getInstance()->getParameter(kiwibot::pylon::RIGHT_CAMERA_S, right);
  createCamera(right_camera, right.c_str());
  ROS_INFO("Left camera");
  std::string left;
  kiwibot::PylonParameters::getInstance()->getParameter(kiwibot::pylon::LEFT_CAMERA_S, left);
  createCamera(left_camera,  left.c_str());

  //Set hardware trigger and execute an AcquisitionStart command to prepare for frame acquisition
  setMasterTrigger(&right_camera);
  setSlaveTrigger(&left_camera);

  right_camera.AcquisitionStart.Execute();
  left_camera.AcquisitionStart.Execute();

  ROS_INFO("Running cameras");
  int loop_rate = 10;
  ros::NodeHandle n("~");
  if(n.hasParam("loop_rate"))
    n.getParam("loop_rate", loop_rate);

  ros::Rate rate(loop_rate);
  ROS_INFO("Attempting to publish: %i Hz", loop_rate);
  int count = 0;

  const char *env = getenv("HOME");
  std::string path = "";
  path.append(env);
  path = path+"/captured-images";
  ROS_INFO("%s", path.c_str());
  while(ros::ok()){
    std_msgs::Header header;
    header.stamp = ros::Time::now();
    right_camera.ExecuteSoftwareTrigger();

    //Get the images
    cv::Mat left_image;
    getImage(&left_camera, left_image);
    cv::Mat right_image;
    getImage(&right_camera, right_image);

    std::string file_name_left  = path+"/left_"+std::to_string(count)+".png";
    std::string file_name_right = path+"/right"+std::to_string(count)+".png";
    count++;
    cv::imwrite(file_name_left, left_image);
    cv::imwrite(file_name_right, right_image);

    //Display images
     cv::imshow("Left", left_image);
     cv::imshow("Right", right_image);
     cv::waitKey(1);

    rate.sleep();
  }
  return 0;
}
