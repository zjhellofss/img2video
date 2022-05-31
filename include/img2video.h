//
// Created by fss on 22-5-31.
//

#ifndef IMG2VIDEO_INCLUDE_IMG2VIDEO_H_
#define IMG2VIDEO_INCLUDE_IMG2VIDEO_H_

#include <iostream>
#include <utility>
#include <glog/logging.h>
#include <experimental/filesystem>
#include <opencv2/opencv.hpp>

struct  AVFrame ;
struct  AVCodecContext;
struct  SwsContext ;
struct  AVFormatContext ;
struct  AVOutputFormat ;


#define MSG_SIZE 512

class ImageConvertor {
 public:
  ~ImageConvertor();

  bool ImageToAvFrame(cv::Mat &image, AVFrame *frame) ;

  void PushFrames(cv::Mat &image) ;

  void FlushFrames();

  void Encode();

  std::vector<std::string> ReadImages() ;

  bool Init();

  ImageConvertor(int width, int height, int bitrate, int fps, std::string output_filename, std::string input_dir)
      : width_(width),
        height_(height),
        bitrate_(bitrate),
        fps_(fps),
        output_filename_(std::move(output_filename)),
        input_dir_(std::move(input_dir)) {}

 private:
  int frame_counter_ = 0;
  int width_;
  int height_;
  int bitrate_;
  int fps_;
  std::string output_filename_;
  std::string input_dir_;
 private:
  AVFrame *video_frame_ = nullptr;
  AVCodecContext *cctx_ = nullptr;
  SwsContext *sws_ctx_ = nullptr;
  AVFormatContext *oc_ = nullptr;
  const AVOutputFormat *format_ = nullptr;
};
#endif //IMG2VIDEO_INCLUDE_IMG2VIDEO_H_
