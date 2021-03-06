#include "network.h"
#include "detection_layer.h"
#include "cost_layer.h"
#include "utils.h"
#include "parser.h"
#include "box.h"
#include "image.h"
#include "demo.h"
#include <sys/time.h>

#define FRAMES 1

#ifdef OPENCV
#include "opencv2/highgui/highgui_c.h"
#include "opencv2/imgproc/imgproc_c.h"
void convert_detections(float *predictions, int classes, int num, int square, int side, int w, int h, float thresh, float **probs, box *boxes, int only_objectness);

static int skip = 0;
static int skipped = 0;

static char **demo_names;
static image *demo_labels;
static int demo_classes;

static float **probs;
static box *boxes;
static network net;
static image in   ;
static image in_s ;
static image det  ;
static image det_s;
static image disp = {0};
static CvCapture * cap;
static float fps = 0;
static float demo_thresh = 0;

static float *predictions[FRAMES];
static int demo_index = 0;
static image images[FRAMES];
static float *avg;

void *fetch_in_thread(void *ptr)
{
    in = get_image_from_stream(cap);
    if(!in.data){
        in = disp;
        if(skipped == skip) error("Stream closed.");
    }else{
        if(disp.data){
            free_image(disp);
        }
        in_s = resize_image(in, net.w, net.h);
    }
    return 0;
}

void *detect_in_thread(void *ptr)
{
    float nms = .4;

    detection_layer l = net.layers[net.n-1];
    float *X = det_s.data;
    float *prediction = network_predict(net, X);

    memcpy(predictions[demo_index], prediction, l.outputs*sizeof(float));
    if(skipped == skip){
        mean_arrays(predictions, FRAMES, l.outputs, avg);
    }

    free_image(det_s);
    convert_detections(avg, l.classes, l.n, l.sqrt, l.side, 1, 1, demo_thresh, probs, boxes, 0);
    if (nms > 0) do_nms(boxes, probs, l.side*l.side*l.n, l.classes, nms);
    printf("\033[2J");
    printf("\033[1;1H");
    printf("\nFPS:%.0f\n",fps);
    printf("Objects:\n\n");

    images[demo_index] = det;
    det = images[(demo_index + FRAMES/2 + 1)%FRAMES];
    demo_index = (demo_index + 1)%FRAMES;

    draw_detections(det, l.side*l.side*l.n, demo_thresh, boxes, probs, demo_names, demo_labels, demo_classes);

    skipped = (skipped + 1)%skip;
    return 0;
}

void demo(char *cfgfile, char *weightfile, float thresh, int cam_index, const char *filename, char **names, image *labels, int classes, int frame_skip)
{
    skip = frame_skip;
    demo_names = names;
    demo_labels = labels;
    demo_classes = classes;
    demo_thresh = thresh;
    printf("Demo\n");
    net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);

    srand(2222222);

    if(filename){
        cap = cvCaptureFromFile(filename);
    }else{
        cap = cvCaptureFromCAM(cam_index);
    }

    if(!cap) error("Couldn't connect to webcam.\n");
    //cvNamedWindow("COCO", CV_WINDOW_NORMAL); 
    //cvResizeWindow("COCO", 512, 512);

    detection_layer l = net.layers[net.n-1];
    int j;

    avg = (float *) calloc(l.outputs, sizeof(float));
    for(j = 0; j < FRAMES; ++j) predictions[j] = (float *) calloc(l.outputs, sizeof(float));
    for(j = 0; j < FRAMES; ++j) images[j] = make_image(1,1,3);

    boxes = (box *)calloc(l.side*l.side*l.n, sizeof(box));
    probs = (float **)calloc(l.side*l.side*l.n, sizeof(float *));
    for(j = 0; j < l.side*l.side*l.n; ++j) probs[j] = (float *)calloc(l.classes, sizeof(float *));

//    pthread_t fetch_thread;
 //   pthread_t detect_thread;

    fetch_in_thread(0);
    det = in;
    det_s = in_s;

    fetch_in_thread(0);
    detect_in_thread(0);
    disp = det;
    det = in;
    det_s = in_s;

    for(j = 0; j < FRAMES/2; ++j){
        fetch_in_thread(0);
        detect_in_thread(0);
        disp = det;
        det = in;
        det_s = in_s;
    }

    int count = 0;
    while(1){
        ++count;
        struct timeval tval_before, tval_after, tval_result;
        gettimeofday(&tval_before, NULL);
        //if(pthread_create(&fetch_thread, 0, fetch_in_thread, 0)) error("Thread creation failed");
        //if(pthread_create(&detect_thread, 0, detect_in_thread, 0)) error("Thread creation failed");
        fetch_in_thread(0);
        detect_in_thread(0);

        disp  = det;
        det   = in;
        det_s = in_s;

        //show_image(disp, "COCO");
        char buff[256];
        sprintf(buff, "coco/coco_%05d", count);
        save_image(disp, buff);

        //free_image(disp);
        //cvWaitKey(10);
        //pthread_join(fetch_thread, 0);
        //pthread_join(detect_thread, 0);

        gettimeofday(&tval_after, NULL);
        timersub(&tval_after, &tval_before, &tval_result);
        float curr = 1000000.f/((long int)tval_result.tv_usec);
        fps = .9*fps + .1*curr;
    }
}
#else
void demo(char *cfgfile, char *weightfile, float thresh, int cam_index, const char *filename, char **names, image *labels, int classes, int frame_skip)
{
    fprintf(stderr, "Demo needs OpenCV for webcam images.\n");
}
#endif

