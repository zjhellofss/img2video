#include "img2video.h"

int main() {
  int fps = 30;
  int width = 1920;
  int height = 1080;
  int bitrate = 2000;
  ImageConvertor convertor(width, height, bitrate, fps, "output.mp4", "./data");
  convertor.Init();
  convertor.Encode();
}