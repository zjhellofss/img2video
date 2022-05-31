#include "img2video.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

bool ImageConvertor::ImageToAvFrame(cv::Mat &image, AVFrame *frame) {
  if (frame && !image.empty()) {
    cv::Mat yuv;
    cv::resize(image, image, cv::Size(this->width_, this->height_), cv::INTER_LINEAR);
    cv::cvtColor(image, yuv, cv::COLOR_BGR2YUV_I420);
    int frame_size = image.cols * image.rows;
    unsigned char *yuv_data = yuv.data;
    frame->data[0] = yuv_data;
    frame->data[1] = yuv_data + frame_size;
    frame->data[2] = yuv_data + frame_size * 5 / 4;
    return true;
  } else {
    LOG(WARNING) << "Frame init failed or image is empty";
    return false;
  }
}
void ImageConvertor::PushFrames(cv::Mat &image) {
  if (!video_frame_) {
    video_frame_ = av_frame_alloc();
    video_frame_->format = AV_PIX_FMT_YUV420P;
    video_frame_->width = cctx_->width;
    video_frame_->height = cctx_->height;
    if (int err = av_frame_get_buffer(video_frame_, 32);err < 0) {
      char err_buf[MSG_SIZE] = {0};
      av_make_error_string(err_buf, MSG_SIZE, err);
      LOG(ERROR) << "Failed to allocate picture: " << err_buf;
      return;
    }
  }
  ImageToAvFrame(image, video_frame_);
  video_frame_->pts = (1.0 / 30.0) * 90000 * (frame_counter_++);

  LOG(INFO) << video_frame_->pts << " " << cctx_->time_base.num << " " <<
            cctx_->time_base.den << " " << frame_counter_;

  if (int err = avcodec_send_frame(cctx_, video_frame_);err < 0) {
    LOG(INFO) << "Failed to send frame" << err;
    return;
  }
  AV_TIME_BASE;
  AVPacket pkt;
  av_init_packet(&pkt);
  pkt.data = nullptr;
  pkt.size = 0;
  pkt.flags |= AV_PKT_FLAG_KEY;
  if (avcodec_receive_packet(cctx_, &pkt) == 0) {
    static int counter = 0;
    LOG(INFO) << "pkt key: " << (pkt.flags & AV_PKT_FLAG_KEY) << " " <<
              pkt.size << " " << (counter++);

    uint8_t *size = ((uint8_t *) pkt.data);
    LOG(INFO) << "first: " << (int) size[0] << " " << (int) size[1] <<
              " " << (int) size[2] << " " << (int) size[3] << " " << (int) size[4] <<
              " " << (int) size[5] << " " << (int) size[6] << " " << (int) size[7];
    av_interleaved_write_frame(oc_, &pkt);
    av_packet_unref(&pkt);
  }
}
void ImageConvertor::FlushFrames() {
  AVPacket pkt;
  av_init_packet(&pkt);
  pkt.data = nullptr;
  pkt.size = 0;

  for (;;) {
    avcodec_send_frame(cctx_, nullptr);
    if (avcodec_receive_packet(cctx_, &pkt) == 0) {
      av_interleaved_write_frame(oc_, &pkt);
      av_packet_unref(&pkt);
    } else {
      break;
    }
  }

  av_write_trailer(oc_);
  if (!(format_->flags & AVFMT_NOFILE)) {
    int err = avio_close(oc_->pb);
    if (err < 0) {
      char err_buf[MSG_SIZE] = {0};
      av_make_error_string(err_buf, MSG_SIZE, err);
      LOG(INFO) << "Failed to close file: " << err_buf;
    }
  }
}
void ImageConvertor::Encode() {
  if (!(format_->flags & AVFMT_NOFILE)) {
    if (int err = avio_open(&oc_->pb, output_filename_.data(), AVIO_FLAG_WRITE);err < 0) {
      char err_buf[MSG_SIZE] = {0};
      av_make_error_string(err_buf, MSG_SIZE, err);
      LOG(ERROR) << "Failed to open file: " << err_buf;
      return;
    }
  }

  if (int err = avformat_write_header(oc_, nullptr);err < 0) {
    char err_buf[MSG_SIZE] = {0};
    av_make_error_string(err_buf, MSG_SIZE, err);
    LOG(ERROR) << "Failed to write header: " << err_buf;
    return;
  }

  av_dump_format(oc_, 0, "test.mp4", 1);

  auto img_paths = this->ReadImages();
  for (const auto &img_path : img_paths) {
    cv::Mat image = cv::imread(img_path);
    PushFrames(image);
  }
  FlushFrames();
}

std::vector<std::string> ImageConvertor::ReadImages() {
  namespace fs = std::experimental::filesystem;
  auto entries = fs::directory_iterator(this->input_dir_);
  std::vector<std::string> img_paths;
  for (const auto &entry : entries) {
    std::string img_path = entry.path().string();
    img_paths.push_back(img_path);
  }
  return img_paths;
}
bool ImageConvertor::Init() {
  format_ = av_guess_format(nullptr, this->output_filename_.data(), nullptr);
  if (!format_) {
    LOG(ERROR) << "Can not create output format: " << this->output_filename_;
    return false;
  }

  if (int err = avformat_alloc_output_context2(&oc_, format_, nullptr, "test.mp4");err != 0) {
    char err_buf[MSG_SIZE] = {0};
    av_make_error_string(err_buf, MSG_SIZE, err);
    LOG(ERROR) << "Can not create output context: " << err_buf;
    return false;
  }

  const AVCodec *codec = nullptr;
  if (codec = avcodec_find_encoder(format_->video_codec);!codec) {
    LOG(ERROR) << "Can not create codec";
  }

  AVStream *stream = avformat_new_stream(oc_, codec);
  if (!stream) {
    LOG(ERROR) << "Can not find format";
    return false;
  }

  cctx_ = avcodec_alloc_context3(codec);
  if (!cctx_) {
    LOG(ERROR) << "Can not create codec context";
    return false;
  }

  stream->codecpar->codec_id = format_->video_codec;
  stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  stream->codecpar->width = width_;
  stream->codecpar->height = height_;
  stream->codecpar->format = AV_PIX_FMT_YUV420P;
  stream->codecpar->bit_rate = bitrate_ * 1000;
  avcodec_parameters_to_context(cctx_, stream->codecpar);
  cctx_->time_base = (AVRational) {1, 1};
  cctx_->max_b_frames = 2;
  cctx_->gop_size = 12;
  cctx_->framerate = (AVRational) {fps_, 1};

  if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
    av_opt_set(cctx_, "preset", "ultrafast", 0);
  } else if (stream->codecpar->codec_id == AV_CODEC_ID_H265) {
    av_opt_set(cctx_, "preset", "ultrafast", 0);
  }

  avcodec_parameters_from_context(stream->codecpar, cctx_);
  if (int err = avcodec_open2(cctx_, codec, nullptr);err < 0) {
    char err_buf[MSG_SIZE] = {0};
    av_make_error_string(err_buf, MSG_SIZE, err);
    LOG(INFO) << "Failed to open codec: " << err_buf;
    return false;
  }
  return true;
}
ImageConvertor::~ImageConvertor() {
  if (video_frame_) {
    av_frame_free(&video_frame_);
  }
  if (cctx_) {
    avcodec_free_context(&cctx_);
  }
  if (oc_) {
    avformat_free_context(oc_);
  }
  if (sws_ctx_) {
    sws_freeContext(sws_ctx_);
  }
}

