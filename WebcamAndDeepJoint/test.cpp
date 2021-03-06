/** @file test.cpp
 *  @brief This is an all-in-one live demo. It combines data acquisition using OpenCV, 2D Joint estimation using Tensorflow ( via VNECT/OpenPose/FORTH estimators ),
 *  and 3D BVH output using MocapNET.
 *  @author Ammar Qammaz (AmmarkoV)

 */

#include "opencv2/opencv.hpp"
using namespace cv;

#include <cstdlib>
#include <unistd.h>

#include "../MocapNETLib/jsonCocoSkeleton.h"
#include "../MocapNETLib/jsonMocapNETHelpers.cpp"

#include "../Tensorflow/tensorflow.hpp"
#include "../MocapNETLib/mocapnet.hpp"
#include "../MocapNETLib/bvh.hpp"
#include "../MocapNETLib/visualization.hpp"

#include "cameraControl.hpp"
#include "utilities.hpp"


#define DISPLAY_ALL_HEATMAPS 0



/**
 * @brief This function performs 2D estimation.. You give her a Tensorflow instance of a 2D estimator, a BGR image some thresholds and sizes and it will yield a vector of 2D points.
 * @ingroup demo
 * @bug This code is oriented to a single 2D skeleton detected, Multiple skeletons will confuse it and there is no logic to handle them
 * @retval A 2D Skeleton detected in the bgr OpenCV image
 */
std::vector<cv::Point_<float> > predictAndReturnSingleSkeletonOf2DCOCOJoints(
    struct TensorflowInstance * net,
    const cv::Mat &bgr ,
    float minThreshold,
    int visualize ,
    unsigned int frameNumber,
    unsigned int inputWidth2DJointDetector,
    unsigned int inputHeight2DJointDetector,
    unsigned int heatmapWidth2DJointDetector,
    unsigned int heatmapHeight2DJointDetector,
    unsigned int numberOfHeatmaps,
    unsigned int numberOfOutputTensors
)
{
    // preprocess image. Actually resize
    float scaleX = (float) inputWidth2DJointDetector/bgr.cols;
    float scaleY = (float) inputHeight2DJointDetector/bgr.rows;
    cv::Mat fr_res;
    cv::resize(bgr, fr_res, cv::Size(0,0), scaleX, scaleY);
    cv::Mat smallBGR = fr_res;//ECONOMY .clone();

    if (visualize)
        {
            cv::imshow("BGR",fr_res);
        }
    fr_res.convertTo(fr_res,CV_32FC3);
    // pass the frame to the Estimator


    std::vector<std::vector<float> > result = predictTensorflowOnArrayOfHeatmaps(
                net,
                (unsigned int) fr_res.cols,
                (unsigned int) fr_res.rows,
                (float*) fr_res.data,
                heatmapWidth2DJointDetector,
                heatmapHeight2DJointDetector,
                numberOfOutputTensors
            );

    if (result.size()<3)
        {
            fprintf(stderr,"Our 2D neural network did not produce an array of 2D heatmaps..\n");
            fprintf(stderr,"Cannot continue with this output...\n");
            std::vector<cv::Point_<float> > emptyVectorOfPoints;
            return emptyVectorOfPoints;
        }


    unsigned int rows = heatmapWidth2DJointDetector;
    unsigned int cols = heatmapHeight2DJointDetector;
    unsigned int hm = numberOfHeatmaps;
    std::vector<cv::Mat> heatmaps;
    for(int i=0; i<hm; ++i)
        {
            cv::Mat h(rows,cols, CV_32FC1);
            for(int r=0; r<rows; ++r)
                {
                    for(int c=0; c<cols; ++c)
                        {
                            int pos = r*cols+c;
                            h.at<float>(r,c) = result[i][pos];
                        }
                }
            heatmaps.push_back(h);
        }


    //This code segment will display every returned heatmap in it's own window..
#if DISPLAY_ALL_HEATMAPS
    if (visualize)
        {
            unsigned int x=0;
            unsigned int y=0;
            char windowLabel[512];
            for(int i=0; i<18; ++i)
                {
                    snprintf(windowLabel,512,"Heatmap %u",i);
                    if (frameNumber==0)
                        {
                            cv::namedWindow(windowLabel,1);
                            cv::moveWindow(windowLabel, x,y);
                        }
                    cv::imshow(windowLabel,heatmaps[i]);
                    y=y+rows+30;
                    if (y>700)
                        {
                            x=x+cols;
                            y=0;
                        }
                }
        }
#endif // DISPLAY_ALL_HEATMAPS

    return dj_getNeuralNetworkDetectionsForColorImage(bgr,smallBGR,heatmaps,minThreshold,visualize,0);
}




/**
 * @brief Convert start and end time to a framerate ( frames per second )
 * @ingroup demo
 * @retval Will return a framerate from two millisecond timestamps, if no time duration has been passed there is no division by zero.
 */
float convertStartEndTimeFromMicrosecondsToFPS(unsigned long startTime, unsigned long endTime)
{
    float timeInMilliseconds =  (float) (endTime-startTime)/1000;
    if (timeInMilliseconds ==0.0)
        {
            timeInMilliseconds=0.00001;    //Take care of division by null..
        }
    return (float) 1000/timeInMilliseconds;
}



/**
 * @brief Retrieve MocapNET output vector from an image
 * @ingroup demo
 * @bug This code is oriented to a single 2D skeleton detected, Multiple skeletons will confuse it and there is no logic to handle them
 * @retval Vector of MocapNET output, in case of error it might be empty..!
 */
std::vector<float> returnMocapNETInputFrom2DDetectorOutput(
    struct TensorflowInstance * net,
    const cv::Mat &bgr,
    struct boundingBox *bbox,
    std::vector<std::vector<float> > & points2DInput,
    float minThreshold,
    int visualize ,
    float * fps,
    unsigned int frameNumber,
    unsigned int offsetX,
    unsigned int offsetY,
    unsigned int stolenWidth,
    unsigned int stolenHeight,
    unsigned int inputWidth2DJointDetector,
    unsigned int inputHeight2DJointDetector,
    unsigned int heatmapWidth2DJointDetector,
    unsigned int heatmapHeight2DJointDetector,
    unsigned int numberOfHeatmaps,
    unsigned int numberOfOutputTensors
)
{
    unsigned int frameWidth  =  bgr.size().width; //frame.cols
    unsigned int frameHeight =  bgr.size().height; //frame.rows

    unsigned long startTime  = GetTickCountMicroseconds();
    std::vector<cv::Point_<float> > pointsOf2DSkeleton = predictAndReturnSingleSkeletonOf2DCOCOJoints(
                net,
                bgr,
                minThreshold,
                visualize,
                frameNumber,
                inputWidth2DJointDetector,
                inputHeight2DJointDetector,
                heatmapWidth2DJointDetector,
                heatmapHeight2DJointDetector,
                numberOfHeatmaps,
                numberOfOutputTensors
            );
    unsigned long endTime = GetTickCountMicroseconds();
    unsigned long openPoseComputationTimeInMilliseconds = (unsigned long) (endTime-startTime)/1000;
    *fps = convertStartEndTimeFromMicrosecondsToFPS(startTime,endTime);
    
    if (!visualize)
        {
            //If we don't visualize using OpenCV output performance
            fprintf(stderr,"OpenPose 2DSkeleton @ %0.2f fps \n",*fps);
        }



    unsigned int i=0;
    struct skeletonCOCO sk= {0};


    if (pointsOf2DSkeleton.size()>=(UT_COCO_PARTS-1))
        {
            // Extract bounding box..

            populateBoundingBox(
                bbox,
                pointsOf2DSkeleton
            );

            for (i=0; i<pointsOf2DSkeleton.size()-1; i++)
                {
                    pointsOf2DSkeleton[i].x+=offsetX;
                    pointsOf2DSkeleton[i].y+=offsetY;
                }

            convertUtilitiesSkeletonFormatToBODY25(&sk,pointsOf2DSkeleton);
            
            
            //----------------------------------------------------------------------------------
            //             Recover points to parent function
            //----------------------------------------------------------------------------------
            points2DInput.clear();
            for (i=0; i<BODY25_PARTS-1; i++)
                {
                    std::vector<float> newPoint;
                    newPoint.push_back(sk.joint2D[i].x);
                    newPoint.push_back(sk.joint2D[i].y);
                    points2DInput.push_back(newPoint);
                }
            //----------------------------------------------------------------------------------

        }
    else
        {
            fprintf(stderr,"Cannot Flatten Empty Skeleton (Got %lu points and had to have at least %u)...\n",pointsOf2DSkeleton.size(),(UT_COCO_PARTS-1));
            std::vector<float> emptyVector;
            return emptyVector;
        }


    return flattenskeletonCOCOToVector(&sk,frameWidth+stolenWidth,frameHeight+stolenHeight);
}



int main(int argc, char *argv[])
{
    fprintf(stderr,"Welcome to the MocapNET demo\n");

    unsigned int forceCPUMocapNET=1;
    unsigned int forceCPU2DJointEstimation=0;

    unsigned int frameNumber=0,skippedFrames=0,frameLimit=5000,frameLimitSet=0,visualize=1;
    float joint2DSensitivity=0.35;
    const char * webcam = 0;

    int live=0,stop=0;
    int constrainPositionRotation=1;
    int doCrop=1,tryForMaximumCrop=0,doSmoothing=3,drawFloor=1,drawNSDM=1;
    int distance = 0,rollValue = 0,pitchValue = 0, yawValue = 0;

    unsigned int quitAfterNSkippedFrames = 10000;
    //2D Joint Detector Configuration
    unsigned int inputWidth2DJointDetector = 368;
    unsigned int inputHeight2DJointDetector = 368;
    unsigned int heatmapWidth2DJointDetector = 46;
    unsigned int heatmapHeight2DJointDetector = 46;
    unsigned int numberOfHeatmaps = 19;
    const char   outputPathStatic[]="out.bvh";
    char * outputPath = (char*) outputPathStatic;
    const char   networkPathOpenPoseMiniStatic[]="combinedModel/openpose_model.pb";
    const char   networkPathVnectStatic[]="combinedModel/vnect_sm_pafs_8.1k.pb";
    const char   networkPathFORTHStatic[]="combinedModel/mobnet2_tiny_vnect_sm_1.9k.pb";

    char   networkInputLayer[]="input_1";
    char   networkOutputLayer[]="k2tfout_0";
    unsigned int numberOfOutputTensors = 3;
    char * networkPath = (char*) networkPathFORTHStatic;
    //-------------------------------

    for (int i=0; i<argc; i++)
        {
            //In order to have an acceptable performance you should run 2D Joint estimation on GPU and MocapNET on CPU (which is the default configuration)
            //If you want to force everything on GPU use --gpu
            //If you want to force everything on CPU use --cpu

            //Switch between different 2D detectors --------------------------------------------------------
            if (strcmp(argv[i],"--openpose")==0)
                {
                    networkPath=(char*) networkPathOpenPoseMiniStatic;
                    networkOutputLayer[8]='1';
                    joint2DSensitivity=0.4;
                    numberOfOutputTensors = 4;
                }
            else if (strcmp(argv[i],"--forth")==0)
                {
                    networkPath=(char*) networkPathFORTHStatic;
                    networkOutputLayer[8]='0';
                    joint2DSensitivity=0.35;
                    numberOfOutputTensors = 3;
                }
            else if (strcmp(argv[i],"--vnect")==0)
                {
                    networkPath = (char*) networkPathVnectStatic;
                    networkOutputLayer[8]='1';
                    joint2DSensitivity=0.20;
                    numberOfOutputTensors = 4;
                }
            else
                // Various other switches -------------------------------------------------------------------
                if (strcmp(argv[i],"--dir")==0)
                    {
                        chdir(argv[i+1]);
                    }
                else if (strcmp(argv[i],"--maxskippedframes")==0)
                    {
                        quitAfterNSkippedFrames=atoi(argv[i+1]);
                    }
                else if (strcmp(argv[i],"--novisualization")==0)
                    {
                        visualize=0;
                    }
                else if (strcmp(argv[i],"--2dmodel")==0)
                    {
                        networkPath=argv[i+1];
                    }
                else if (strcmp(argv[i],"--output")==0)
                    {
                        outputPath=argv[i+1];
                    }
                else if (strcmp(argv[i],"-o")==0)
                    {
                        outputPath=argv[i+1];
                    }
                else if (strcmp(argv[i],"--frames")==0)
                    {
                        frameLimit=atoi(argv[i+1]);
                        frameLimitSet=1;
                    }
                else
                    //if (strcmp(argv[i],"--cpu")==0)           { setenv("CUDA_VISIBLE_DEVICES", "", 1); } else //Alternate way to force CPU everywhere
                    if (strcmp(argv[i],"--cpu")==0)
                        {
                            forceCPUMocapNET=1;
                            forceCPU2DJointEstimation=1;
                        }
                    else if (strcmp(argv[i],"--gpu")==0)
                        {
                            forceCPUMocapNET=0;
                            forceCPU2DJointEstimation=0;
                        }
                    else if (strcmp(argv[i],"--unconstrained")==0)
                        {
                            constrainPositionRotation=0;
                        }
                    else if (strcmp(argv[i],"--nocrop")==0)
                        {
                            doCrop=0;
                        }
                    else if (strcmp(argv[i],"--live")==0)
                        {
                            live=1;
                            frameLimit=0;
                        }
                    else if (strcmp(argv[i],"--from")==0)
                        {
                            if (argc>i+1)
                                {
                                    webcam = argv[i+1];
                                }
                        }
        }


    if (initializeBVHConverter())
        {
            fprintf(stderr,"BVH allocation happened we are going to have BVH visualization \n");
        }

    fprintf(stderr,"Attempting to open input device\n");
    cv::Mat controlMat = Mat(Size(inputWidth2DJointDetector,2),CV_8UC3, Scalar(0,0,0));

    VideoCapture cap(webcam); // open the default camera
    if (webcam==0)
        {
            fprintf(stderr,"Trying to open webcam\n");
            cap.set(CV_CAP_PROP_FRAME_WIDTH,640);
            cap.set(CV_CAP_PROP_FRAME_HEIGHT,480);
        }
    else
        {
            fprintf(stderr,"Trying to open %s\n",webcam);
        }

    if (!cap.isOpened())  // check if succeeded to connect to the camera
        {
            fprintf(stderr,"Openning input stream `%s` failed\n",webcam);
            return 1;
        }

    signed int totalNumberOfFrames = cap.get(CV_CAP_PROP_FRAME_COUNT);
    fprintf(stderr,"totalNumberOfFrames in %s is %u \n",webcam,totalNumberOfFrames);
    if ( (totalNumberOfFrames>0) && (!frameLimitSet) )
    {
     frameLimit=totalNumberOfFrames;   
    }

    cv::Mat frame;
    struct boundingBox cropBBox= {0};
    unsigned int croppedDimensionWidth=0,croppedDimensionHeight=0,offsetX=0,offsetY=0;

    struct TensorflowInstance net= {0};
    struct MocapNET mnet= {0};



    std::vector<float> flatAndNormalizedPoints;
    std::vector<std::vector<float> > bvhFrames;
    std::vector<float> previousBvhOutput;
    std::vector<float> bvhOutput;
    std::vector<std::vector<float> > points2DOutput;
    std::vector<std::vector<float> > points2DOutputGUIForcedView;

    if ( loadMocapNET(&mnet,"test",forceCPUMocapNET) )
        {
            if (
                loadTensorflowInstance(
                    &net,
                    networkPath,
                    networkInputLayer,
                    networkOutputLayer,
                    forceCPU2DJointEstimation
                )

            )
                {
                    frameNumber=0;
                    while ( ( (live) || (frameNumber<frameLimit) ) &&  (!stop) )
                        {
                            // Get Image
                            unsigned long acquisitionStart = GetTickCountMicroseconds();

                            cap >> frame; // get a new frame from camera
                            cv::Mat frameOriginal = frame; //ECONOMY .clone();

                            unsigned int frameWidth  =  frame.size().width;  //frame.cols
                            unsigned int frameHeight =  frame.size().height; //frame.rows
                            
                            
                            //-------------------------------------------------
                            //          Visualization Window Size
                            //-------------------------------------------------
                            unsigned int visWidth=frameWidth;
                            unsigned int visHeight=frameHeight;
                            //If our input window is small enlarge it a little..
                            if (visWidth<700)
                                  {
                                    visWidth=(unsigned int) frameWidth*2.0;
                                    visHeight=(unsigned int) frameHeight*2.0;
                                  }
                            visWidth=1024;
                            visHeight=768;
                            //-------------------------------------------------
                            
                            
                            if ( (frameWidth!=0)  && (frameHeight!=0)  )
                                {
                                    unsigned long acquisitionEnd = GetTickCountMicroseconds();


                                    float fpsAcquisition = convertStartEndTimeFromMicrosecondsToFPS(acquisitionStart,acquisitionEnd);

                                    //------------------------------------------------------------------------
                                    // If cropping is enabled
                                    if (doCrop)
                                        {
                                            //And there was some previous BVH output
                                            if (bvhOutput.size()>0)
                                                {
                                                    // Try to crop around the last closest
                                                    //------------------------------------------------------------------------
                                                    if (
                                                        getBestCropWindow(
                                                            tryForMaximumCrop,
                                                            &offsetX,
                                                            &offsetY,
                                                            &croppedDimensionWidth,
                                                            &croppedDimensionHeight,
                                                            &cropBBox,
                                                            inputWidth2DJointDetector,
                                                            inputHeight2DJointDetector,
                                                            frameWidth,
                                                            frameHeight
                                                        )
                                                    )
                                                        {
                                                            if (croppedDimensionWidth!=croppedDimensionHeight)
                                                                {
                                                                    fprintf(stderr,"Bounding box produced was not a rectangle (%ux%u)..!\n",croppedDimensionWidth,croppedDimensionHeight);
                                                                }
                                                            cv::Rect rectangleROI(offsetX,offsetY,croppedDimensionWidth,croppedDimensionHeight);
                                                            frame = frame(rectangleROI);
                                                            cropBBox.populated=0;
                                                        }
                                                }
                                            else
                                                {
                                                    fprintf(stderr,"Haven't detected a person, so seeking a skeleton in the full image, regardless of distortion..\n");
                                                }
                                        }
                                    //------------------------------------------------------------------------

                                    if ( (frameWidth>0) && (frameHeight>0) )
                                        {
                                            std::vector<std::vector<float> >  points2DInput;
                                            
                                            // Get 2D Skeleton Input from Frame
                                            float fps2DJointDetector = 0;
                                            flatAndNormalizedPoints = returnMocapNETInputFrom2DDetectorOutput(
                                                                          &net,
                                                                          frame,
                                                                          &cropBBox,
                                                                          points2DInput,
                                                                          joint2DSensitivity,
                                                                          visualize,
                                                                          &fps2DJointDetector,
                                                                          frameNumber,
                                                                          offsetX,
                                                                          offsetY,
                                                                          frameWidth-croppedDimensionWidth,
                                                                          frameHeight-croppedDimensionHeight,
                                                                          inputWidth2DJointDetector,
                                                                          inputHeight2DJointDetector,
                                                                          heatmapWidth2DJointDetector,
                                                                          heatmapHeight2DJointDetector,
                                                                          numberOfHeatmaps,
                                                                          numberOfOutputTensors
                                                                      );

                                            // Get MocapNET prediction
                                            unsigned long startTime = GetTickCountMicroseconds();
                                            bvhOutput = runMocapNET(&mnet,flatAndNormalizedPoints);
                                            unsigned long endTime = GetTickCountMicroseconds();
                                            
                                            //-------------------------------------------------------------------------------------------------------------------------
                                            // Adding a vary baseline smoothing after a project request, it should be noted
                                            // that no results during the original BMVC2019 used any smoothing of any kind 
                                            //-------------------------------------------------------------------------------------------------------------------------
                                            if(doSmoothing)
                                            {//-------------------------------------------------------------------------------------------------------------------------
                                                 if ( (previousBvhOutput.size()>0) && (bvhOutput.size()>0) )
                                                 {
                                                     smoothVector(previousBvhOutput,bvhOutput,(float) doSmoothing/10);
                                                    previousBvhOutput=bvhOutput;
                                                 }
                                            }//-------------------------------------------------------------------------------------------------------------------------
                                            

                                            float fpsMocapNET = convertStartEndTimeFromMicrosecondsToFPS(startTime,endTime);


                                             std::vector<float> bvhForcedViewOutput=bvhOutput;

                                            if (!visualize)
                                                {
                                                    //If we don't visualize using OpenCV output performance
                                                    fprintf(stderr,"MocapNET 3DSkeleton @ %0.2f fps \n",fpsMocapNET);
                                                }

                                            //If we are not running live ( aka not from a webcam with no fixed frame limit )
                                            //Then we record the current bvh frame in order to save a .bvh file in the end..
                                            if (!live)
                                                {
                                                    bvhFrames.push_back(bvhOutput);
                                                }


                                            //Force Skeleton Position and orientation to make it more easily visualizable
                                            if (constrainPositionRotation)
                                                {
                                                    if (bvhForcedViewOutput.size()>0)
                                                        {
                                                            bvhForcedViewOutput[MOCAPNET_OUTPUT_HIP_XPOSITION]=0.0;
                                                            bvhForcedViewOutput[MOCAPNET_OUTPUT_HIP_YPOSITION]=0.0;
                                                            bvhForcedViewOutput[MOCAPNET_OUTPUT_HIP_ZPOSITION]=-160.0 - (float) distance;
                                                            bvhForcedViewOutput[MOCAPNET_OUTPUT_HIP_ZROTATION]=(float) rollValue;
                                                            bvhForcedViewOutput[MOCAPNET_OUTPUT_HIP_YROTATION]=(float) yawValue;
                                                            bvhForcedViewOutput[MOCAPNET_OUTPUT_HIP_XROTATION]=(float) pitchValue;
                                                        }
                                                }

                                            if (bvhForcedViewOutput.size()>0)
                                            { 
                                              points2DOutputGUIForcedView = convertBVHFrameTo2DPoints(bvhForcedViewOutput,visWidth,visHeight);
                                            }

                                            if (bvhOutput.size()>0)
                                            { 
                                              points2DOutput = convertBVHFrameTo2DPoints(
                                                                                          bvhOutput,
                                                                                          1920,//visWidth,
                                                                                          1080//visHeight
                                                                                        );
                                            }
                                            
                                             

                                            //Display two sample joint configurations to console output
                                            //to demonstrate how easy it is to get the output joint information
                                            if (bvhOutput.size()>0)
                                                {
                                                    fprintf(stderr,"Right Shoulder Z X Y = %0.2f,%0.2f,%0.2f\n",
                                                            bvhOutput[MOCAPNET_OUTPUT_RSHOULDER_ZROTATION],
                                                            bvhOutput[MOCAPNET_OUTPUT_RSHOULDER_XROTATION],
                                                            bvhOutput[MOCAPNET_OUTPUT_RSHOULDER_YROTATION]
                                                           );

                                                    fprintf(stderr,"Left Shoulder Z X Y = %0.2f,%0.2f,%0.2f\n",
                                                            bvhOutput[MOCAPNET_OUTPUT_LSHOULDER_ZROTATION],
                                                            bvhOutput[MOCAPNET_OUTPUT_LSHOULDER_XROTATION],
                                                            bvhOutput[MOCAPNET_OUTPUT_LSHOULDER_YROTATION]
                                                           );
                                                }


                                            unsigned long totalTimeEnd = GetTickCountMicroseconds();
                                            float fpsTotal = convertStartEndTimeFromMicrosecondsToFPS(acquisitionStart,totalTimeEnd);


                                            //OpenCV Visualization stuff
                                            //---------------------------------------------------
                                            if (visualize)
                                                { 
                                                    if (frameNumber==0)
                                                        {
                                                            cv::imshow("3D Control",controlMat);

                                                            createTrackbar("Stop Demo", "3D Control", &stop, 1);
                                                            createTrackbar("Constrain Position/Rotation", "3D Control", &constrainPositionRotation, 1);
                                                            createTrackbar("Automatic Crop", "3D Control", &doCrop, 1); 
                                                            createTrackbar("Smooth 3D Output", "3D Control", &doSmoothing, 10);
                                                            createTrackbar("Maximize Crop", "3D Control", &tryForMaximumCrop, 1);
                                                            createTrackbar("Draw Floor", "3D Control", &drawFloor, 1);
                                                            createTrackbar("Draw NSDM", "3D Control", &drawNSDM, 1);
                                                            createTrackbar("Distance  ", "3D Control", &distance,  150);
                                                            createTrackbar("Yaw            ", "3D Control", &yawValue,  360);
                                                            createTrackbar("Pitch          ", "3D Control", &pitchValue,360);
                                                            createTrackbar("Roll            ", "3D Control", &rollValue, 360);



                                                            cv::namedWindow("3D Points Output");
                                                            cv::moveWindow("3D Control",inputWidth2DJointDetector,inputHeight2DJointDetector);
                                                            cv::moveWindow("2D NN Heatmaps",0,0);
                                                            cv::moveWindow("BGR",0,inputHeight2DJointDetector);
                                                            //cv::moveWindow("2D Detections",inputWidth2DJointDetector,0);
                                                        }


                                                    //Get rid of GLib-GObject-CRITICAL **: 10:36:18.934: g_object_unref: assertion 'G_IS_OBJECT (object)' failed opencv
                                                    //by displaying an empty cv Mat on the window besides the trackbars
                                                    cv::imshow("3D Control",controlMat);


                                                    visualizePoints(
                                                        "3D Points Output",
                                                        frameNumber,
                                                        skippedFrames,
                                                        totalNumberOfFrames,
                                                        frameLimit,
                                                        drawFloor,
                                                        drawNSDM,
                                                        fpsTotal,
                                                        fpsAcquisition,
                                                        fps2DJointDetector,
                                                        fpsMocapNET,
                                                        visWidth,
                                                        visHeight,
                                                        0,
                                                        flatAndNormalizedPoints,
                                                        bvhOutput,
                                                        bvhForcedViewOutput,
                                                        points2DInput,
                                                        points2DOutput,
                                                        points2DOutputGUIForcedView
                                                    );


                                                    if (frameNumber==0)
                                                        {
                                                            cv::resizeWindow("3D Control",inputWidth2DJointDetector,inputHeight2DJointDetector);
                                                            cv::moveWindow("3D Points Output",inputWidth2DJointDetector*2,0);
                                                            cv::moveWindow("2D Detections",inputWidth2DJointDetector,0);
                                                        }


                                                    //Window Event Loop Time and  Receiving Key Presses..
                                                    //----------------------------------------------------------------------------------------------------------
                                                    int key = cv::waitKey(1) ;
                                                    key = 0x000000FF & key;

                                                    if (key!=255)
                                                        {
                                                            fprintf(stderr,"Keypress = %u \n",key);
                                                            if  (
                                                                (key == 113) ||
                                                                (key == 81)
                                                            )
                                                                {
                                                                    fprintf(stderr,"Stopping MocapNET after keypress..\n");
                                                                    stop=1;
                                                                } // stop capturing by pressing q
                                                        }
                                                    //----------------------------------------------------------------------------------------------------------

                                                }
                                            //---------------------------------------------------
                                        }



                                    ++frameNumber;
                                }
                            else
                                {
                                    if (totalNumberOfFrames>0)
                                        {
                                            if (skippedFrames+frameNumber>=totalNumberOfFrames)
                                                {
                                                    fprintf(stderr,GREEN "Stream appears to have ended..\n" NORMAL);
                                                    break;
                                                }
                                            else
                                                {
                                                    ++skippedFrames;
                                                }
                                        }
                                    else
                                        {
                                            ++skippedFrames;
                                        }

                                    fprintf(stderr,YELLOW "OpenCV failed to snap frame %u from your input source (%s)\n" NORMAL,frameNumber,webcam);
                                    fprintf(stderr,NORMAL "Skipped frames %u/%u\n" NORMAL,skippedFrames,frameNumber);

                                    if (skippedFrames>quitAfterNSkippedFrames)
                                        {
                                            fprintf(stderr,RED "We have encountered %u skipped frames so quitting ..\n" NORMAL,quitAfterNSkippedFrames);
                                            fprintf(stderr,RED "If you don't want this to happen consider using the flag --maxskippedframes and providing a bigger value ..\n" NORMAL);
                                            break;
                                        }
                                }

                        } //Master While Frames Exist loop

                    //After beeing done with the frames gathered the bvhFrames vector should be full of our data, so maybe we want to write it to a file..!
                    if (!live)
                        {
                            fprintf(stderr,"Will now write BVH file to %s.. \n",outputPath);
                            //just use BVH header
                            if ( writeBVHFile(outputPath,0,bvhFrames) )
                                {
                                    fprintf(stderr,GREEN "Successfully wrote %lu frames to bvh file.. \n" NORMAL,bvhFrames.size());
                                }
                            else
                                {
                                    fprintf(stderr,RED "Failed to write %lu frames to bvh file.. \n" NORMAL,bvhFrames.size());
                                }
                            if (skippedFrames>0)
                                {
                                    fprintf(stderr,"Please note that while getting input %u frames where skipped due to OpenCV related errors\n",skippedFrames);
                                }
                        }
                    else
                        {
                            fprintf(stderr,"Will not record a bvh file since live sessions can be arbitrarily long..\n");
                        }
                }
            else
                {
                    fprintf(stderr,"Was not able to load Tensorflow model for 2D joint detection..\n");
                }
        }
    else
        {
            fprintf(stderr,"Was not able to load MocapNET, please make sure you have the appropriate models downloaded..\n");
        }
    // the camera will be deinitialized automatically in VideoCapture destructor


    fprintf(stderr,NORMAL "MocapNET Live Demo finished.\n" NORMAL);
    return 0;
}
