#include <ros/ros.h>
#include <nodelet/nodelet.h>
#include <image_transport/image_transport.h>

#include <cv_bridge/cv_bridge.h>
#include <opencv2/highgui/highgui.hpp>
#include "window_thread.h"

#include <boost/thread.hpp>
#include <boost/format.hpp>

//#include <std>

#ifdef HAVE_GTK
#include <gtk/gtk.h>

using namespace std;

// Platform-specific workaround for #3026: image_view doesn't close when
// closing image window. On platforms using GTK+ we connect this to the
// window's "destroy" event so that image_view exits.
static void destroyNode(GtkWidget *widget, gpointer data)
{
  /// @todo On ros::shutdown(), the node hangs. Why?
  //ros::shutdown();
  exit(0); // brute force solution
}

static void destroyNodelet(GtkWidget *widget, gpointer data)
{
  // We can't actually unload the nodelet from here, but we can at least
  // unsubscribe from the image topic.
  reinterpret_cast<image_transport::Subscriber*>(data)->shutdown();
}
#endif


namespace image_view {

class ImageNodelet : public nodelet::Nodelet
{
  image_transport::Subscriber sub_;

  boost::mutex image_mutex_;
  sensor_msgs::ImageConstPtr last_msg_;
  cv::Mat last_image_;
  
  std::string window_name_;
  boost::format filename_format_;
  int count_;

  virtual void onInit();
  
  void imageCb(const sensor_msgs::ImageConstPtr& msg);

  static void mouseCb(int event, int x, int y, int flags, void* param);

public:
  ImageNodelet();

  ~ImageNodelet();
};

ImageNodelet::ImageNodelet()
  : filename_format_(""), count_(0)
{
}

ImageNodelet::~ImageNodelet()
{
  cv::destroyWindow(window_name_);
}

void ImageNodelet::onInit()
{
  printf("===========entered onInit() in image_nodelett.cpp================\n");
  ros::NodeHandle nh = getNodeHandle();
  ros::NodeHandle local_nh = getPrivateNodeHandle();

  // Command line argument parsing
  const std::vector<std::string>& argv = getMyArgv();
  // First positional argument is the transport type
  std::string transport = "raw";
  for (int i = 0; i < (int)argv.size(); ++i)
  {
    if (argv[i][0] != '-')
    {
      transport = argv[i];
      break;
    }
  }
  // Internal option, should be used only by the image_view node
  bool shutdown_on_close = std::find(argv.begin(), argv.end(),
                                     "--shutdown-on-close") != argv.end();

  // Default window name is the resolved topic name
  std::string topic = nh.resolveName("image");
  local_nh.param("window_name", window_name_, topic);

  bool autosize;
  local_nh.param("autosize", autosize, false);
  
  std::string format_string;
  local_nh.param("filename_format", format_string, std::string("frame%04i.jpg"));
  filename_format_.parse(format_string);

  cv::namedWindow(window_name_, autosize ? CV_WINDOW_AUTOSIZE : 0);
  cv::setMouseCallback(window_name_, &ImageNodelet::mouseCb, this);
  
#ifdef HAVE_GTK
  // Register appropriate handler for when user closes the display window
  GtkWidget *widget = GTK_WIDGET( cvGetWindowHandle(window_name_.c_str()) );
  if (shutdown_on_close)
    g_signal_connect(widget, "destroy", G_CALLBACK(destroyNode), NULL);
  else
    g_signal_connect(widget, "destroy", G_CALLBACK(destroyNodelet), &sub_);
#endif

  // Start the OpenCV window thread so we don't have to waitKey() somewhere
  startWindowThread();

  image_transport::ImageTransport it(nh);
  image_transport::TransportHints hints(transport, ros::TransportHints(), getPrivateNodeHandle());
  sub_ = it.subscribe(topic, 1, &ImageNodelet::imageCb, this, hints);
}

void ImageNodelet::imageCb(const sensor_msgs::ImageConstPtr& msg)
{
  image_mutex_.lock();
  
  cout<<"===========entered imageCb in image_nodelett.cpp================\n";

  // May want to view raw bayer data, which CvBridge doesn't know about
  if (msg->encoding.find("bayer") != std::string::npos)
  {
    last_image_ = cv::Mat(msg->height, msg->width, CV_8UC1,
                          const_cast<uint8_t*>(&msg->data[0]), msg->step);
  }
  // We want to scale floating point images so that they display nicely
  else if(msg->encoding.find("F") != std::string::npos)
  {
    cv::Mat float_image_bridge = cv_bridge::toCvShare(msg, msg->encoding)->image;
    cv::Mat_<float> float_image = float_image_bridge;
    float max_val = 0;
    for(int i = 0; i < float_image.rows; ++i)
    {
      for(int j = 0; j < float_image.cols; ++j)
      {
        max_val = std::max(max_val, float_image(i, j));
      }
    }

    if(max_val > 0)
    {
      float_image /= max_val;
    }
    last_image_ = float_image;
  }
  else
  {
    // Convert to OpenCV native BGR color
    try {
      last_image_ = cv_bridge::toCvShare(msg, "bgr8")->image;
    }
    catch (cv_bridge::Exception& e) {
      NODELET_ERROR_THROTTLE(30, "Unable to convert '%s' image to bgr8: '%s'",
                             msg->encoding.c_str(), e.what());
    }
  }

  // last_image_ may point to data owned by last_msg_, so we hang onto it for
  // the sake of mouseCb.
  last_msg_ = msg;

  // Must release the mutex before calling cv::imshow, or can deadlock against
  // OpenCV's window mutex.
  image_mutex_.unlock();
  if (!last_image_.empty())
    cv::imshow(window_name_, last_image_);
}

void ImageNodelet::mouseCb(int event, int x, int y, int flags, void* param)
{
  printf("===========entered mouseCb in image_nodelett.cpp================\n");
  cout<<"===========entered mouseCb in image_nodelett.cpp================\n";
  
  ImageNodelet *this_ = reinterpret_cast<ImageNodelet*>(param);
  // Trick to use NODELET_* logging macros in static function
  boost::function<const std::string&()> getName =
    boost::bind(&ImageNodelet::getName, this_);
  
  NODELET_INFO("===========entered mouseCb in image_nodelett.cpp================\n");
  
  if (event == CV_EVENT_LBUTTONDOWN)
  {
    NODELET_WARN_ONCE("Left-clicking no longer saves images. Right-click instead.");
    return;
  }
  if (event != CV_EVENT_RBUTTONDOWN)
    return;
  
  boost::lock_guard<boost::mutex> guard(this_->image_mutex_);

  const cv::Mat image = this_->last_image_;
  if (image.empty())
  {
    NODELET_WARN("Couldn't save image, no data!");
    return;
  }

  std::string filename = (this_->filename_format_ % this_->count_).str();
  if (cv::imwrite(filename, image))
  {
    //NODELET_INFO("Saved image %s", filename.c_str());
    this_->count_++;
  }
  else
  {
    /// @todo Show full path, ask if user has permission to write there
    NODELET_ERROR("Failed to save image.");
  }
}

} // namespace image_view

// Register the nodelet
#include <pluginlib/class_list_macros.h>
PLUGINLIB_DECLARE_CLASS(image_view, image, image_view::ImageNodelet, nodelet::Nodelet)
