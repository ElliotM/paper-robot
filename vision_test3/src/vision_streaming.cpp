#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <cv.h>
#include <stdlib.h>

using namespace cv;

namespace enc = sensor_msgs::image_encodings;

static const char WINDOW[] = "Image window";
static const char CANNY_WINDOW[] = "Canny Window";
static const char THRESHOLD_WINDOW[] = "Threshold Window";

void thresh_callback(int, void* );

class ImageConverter
{
  ros::NodeHandle nh_;
  image_transport::ImageTransport it_;
  image_transport::Subscriber image_sub_;
  image_transport::Publisher image_pub_;
  
  Mat outputMat;
  int threshCanny;
  int threshVal;
  int max_thresh;
  int minRectSize;
  int maxRectSize;
  RNG rng;
  Mat src;
  
  //public: static ImageConverter instance;// = 0;
  
  public: ImageConverter(bool isStaticImage, char* staticImage) : it_(nh_)
  {
    ROS_INFO("===========Entered constructor===========");
    threshCanny = 255;
    threshVal = 117;
    max_thresh = 255;
    minRectSize = 1200;
    maxRectSize = 50000;
    rng = RNG(12345);

    namedWindow(WINDOW);
    namedWindow(CANNY_WINDOW);
    namedWindow(THRESHOLD_WINDOW);
    
    createTrackbar( " Canny thresh:", CANNY_WINDOW, &threshCanny, max_thresh, &processImageCallback, this );
    createTrackbar( " Thresholding:", THRESHOLD_WINDOW, &threshVal, max_thresh, &processImageCallback, this );
    createTrackbar( " Min Rect Size:", WINDOW, &minRectSize, maxRectSize, &processImageCallback, this );
    
    ROS_INFO("Initialization finished.");
    //image_pub_ = it_.advertise("out", 1);
    if (!isStaticImage) {//staticImage[0] == '\0') {
        ROS_INFO("No static image - stream from camera.");
        image_sub_ = it_.subscribe("/wide_stereo/left/image_rect_color", 1, &ImageConverter::imageCb, this);
    }
    else {
        ROS_INFO("Static image found.");
        ROS_INFO(staticImage);
        src = imread(staticImage, 1 );
        processImageCallback(0, this);
        waitKey(0);
    }
  }

  ~ImageConverter()
  {
    cv::destroyWindow(WINDOW);
  }
  
  static void processImageCallback(int arg, void* context) {
    //((ImageConverter*)context)->processImage();
    ImageConverter* self = static_cast<ImageConverter*>(context);
    self->processImage();
  }
  
  /*static void processImageCallback(int other_arg, void * this_pointer) {
    ImageConverter * self = static_cast<ImageConverter*>(this_pointer);
    self->processImage();
  }*/
  
  void processImage() {
    //break into color spaces for comparison
    //rgb
    Mat image = src.clone();
    vector<Mat> rgb;
    split(image, rgb);  

    //luv
    Mat luvMat;
    vector<Mat> luv;
    cvtColor( image, luvMat, CV_BGR2Luv );
    split(luvMat, luv);

    //hsv
    Mat hsvMat;
    vector<Mat> hsv;
    cvtColor( image, hsvMat, CV_BGR2HSV );
    split(hsvMat, hsv);

    //blur the channels
    blur( rgb[0], rgb[0], Size(3,3) );
    blur( rgb[1], rgb[1], Size(3,3) );
    blur( rgb[2], rgb[2], Size(3,3) );

    blur( luv[0], luv[0], Size(3,3) );
    blur( luv[1], luv[1], Size(3,3) );
    blur( luv[2], luv[2], Size(3,3) );

    blur( hsv[0], hsv[0], Size(3,3) );
    blur( hsv[1], hsv[1], Size(3,3) );
    blur( hsv[2], hsv[2], Size(3,3) );

    //output
    namedWindow( "Red", CV_WINDOW_AUTOSIZE );
    imshow( "Red", rgb[2] );
    /*namedWindow( "Blue", CV_WINDOW_AUTOSIZE );
    imshow( "Blue", rgb[1] );
    namedWindow( "Green", CV_WINDOW_AUTOSIZE );
    imshow( "Green", rgb[0] );

    namedWindow( "L", CV_WINDOW_AUTOSIZE );
    imshow( "L", luv[2] );
    namedWindow( "U", CV_WINDOW_AUTOSIZE );
    imshow( "U", luv[1] );
    namedWindow( "lV", CV_WINDOW_AUTOSIZE );
    imshow( "lV", luv[0] );

    namedWindow( "H", CV_WINDOW_AUTOSIZE );
    imshow( "H", hsv[2] );
    namedWindow( "S", CV_WINDOW_AUTOSIZE );
    imshow( "S", hsv[1] );
    namedWindow( "hV", CV_WINDOW_AUTOSIZE );
    imshow( "hV", hsv[0] );*/

    /// Convert image to gray and blur it
    //cvtColor( image, src_gray, CV_BGR2GRAY );
    //blur( src_gray, src_gray, Size(3,3) );
    
    //set image to be processed
    outputMat = rgb[2];
    
    //begin processing the image
    Mat canny_output;
    Mat threshedMat;
    vector<vector<Point> > contours;
    vector<Vec4i> hierarchy;

    //threshedMat = outputMat;
    threshold(outputMat, threshedMat, threshVal, 255, CV_THRESH_BINARY);

    /// Detect edges using canny
    //canny_output = threshedMat;
    Canny( threshedMat, canny_output, threshCanny, threshCanny*2, 3 );

    /// Find contours
    findContours( canny_output, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, Point(0, 0) );

    /// Approximate contours to polygons + get bounding rects and circles
    vector<vector<Point> > contours_poly( contours.size() );
    vector<Rect> boundRect( contours.size() );
    vector<Point2f>center( contours.size() );
    vector<float>radius( contours.size() );

    for( int i = 0; i < contours.size(); i++ ) { 
        approxPolyDP( Mat(contours[i]), contours_poly[i], 3, true );
        boundRect[i] = boundingRect( Mat(contours_poly[i]) );
        minEnclosingCircle( (Mat)contours_poly[i], center[i], radius[i] );
        if (boundRect[i].area() < minRectSize) {
            contours.erase(contours.begin() + i);
            contours_poly.erase(contours_poly.begin() + i);
            boundRect.erase(boundRect.begin() + i);
            center.erase(center.begin() + i);
            radius.erase(radius.begin() + i);
            i--;
        }
    }

    /// Draw contours
    //Mat drawing = Mat::zeros( canny_output.size(), CV_8UC3 );
    for( int i = 0; i < contours.size(); i++ ) {
        Scalar color = Scalar(0, 0, 255);//( rng.uniform(0, 255), rng.uniform(0,255), rng.uniform(0,255) );
        drawContours( image, contours_poly, i, color, 1, 8, vector<Vec4i>(), 0, Point() );
        rectangle( image, boundRect[i].tl(), boundRect[i].br(), color, 2, 8, 0 );
        circle( image, center[i], 1, color, 2, 8, 0 );
        //circle( drawing, center[i], (int)radius[i], color, 2, 8, 0 );
    }
    
    //select the largest Rect from those that remain
    int largestRect = 0;
    for (int i = 0; i < boundRect.size(); i++) {
        if (boundRect[i].area() > boundRect[largestRect].area()) {
            largestRect = i;
        }
    }
    if (boundRect.size() > 0) {
        //TODO: return that rect
        //NOTE: there is a known g++ error that does not let you use the to_string() function in std.  
        //there are patches, but i have not fully looked into them.  
        // http://stackoverflow.com/questions/12975341/to-string-is-not-a-member-of-std-says-so-g
        //ROS_INFO(std::to_string(boundRect[largestRect].area())); 
        
        //printf("The area of the paper is:  %i\n", boundRect[largestRect].area());
    }
    
    waitKey(5);
    imshow(WINDOW, image);
    waitKey(5);
    imshow(CANNY_WINDOW, canny_output);
    waitKey(5);
    imshow(THRESHOLD_WINDOW, threshedMat);
    waitKey(5);
    
    //image_pub_.publish(cv_ptr->toImageMsg());
  }

  void imageCb(const sensor_msgs::ImageConstPtr& msg)
  {
    //covert the rosimage into an openCV Mat image
    cv_bridge::CvImagePtr cv_ptr;
    try
    {
      cv_ptr = cv_bridge::toCvCopy(msg, enc::BGR8);
    }
    catch (cv_bridge::Exception& e)
    {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return;
    }
    
    //store the new Mat image inside of a Mat object for easy access
    src = cv_ptr->image;
    
    processImage();
  }
};

void thresh_callback(int, void* )
{
}

int main(int argc, char** argv)
{
  ROS_INFO("===========Entered main===========");
  ros::init(argc, argv, "image_converter");
  //ImageConverter ImageConverter::instance = ImageConverter(argc > 1, argv[1]);
  ImageConverter ic(argc > 1, argv[1]);
  ros::spin();
  return 0;
}
