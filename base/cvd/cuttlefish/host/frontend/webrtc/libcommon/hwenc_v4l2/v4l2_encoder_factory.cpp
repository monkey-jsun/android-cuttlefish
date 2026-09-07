/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/frontend/webrtc/libcommon/hwenc_v4l2/v4l2_encoder_factory.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

// Linux
#include <linux/videodev2.h>
#include <sys/ioctl.h>

// WebRTC
#include <rtc_base/logging.h>

#include "cuttlefish/host/frontend/webrtc/libcommon/hwenc_v4l2/v4l2_h264_encoder.h"

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

constexpr char kH264CodecName[] = "H264";

// True if |fd|'s CAPTURE (coded) side can produce H.264, i.e. it is an encoder.
bool CaptureSupportsH264(int fd) {
  for (int i = 0;; i++) {
    v4l2_fmtdesc fmtdesc = {};
    fmtdesc.index = i;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
      break;  // no more formats
    }
    if (fmtdesc.pixelformat == V4L2_PIX_FMT_H264) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::string FindV4L2H264EncoderDevice() {
  for (int n = 0; n < 64; n++) {
    char path[32];
    std::snprintf(path, sizeof(path), "/dev/video%d", n);
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }

    v4l2_capability cap = {};
    bool is_h264_m2m = false;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
      uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                          ? cap.device_caps
                          : cap.capabilities;
      // A memory-to-memory multiplanar device whose coded side is H.264.
      if ((caps & V4L2_CAP_VIDEO_M2M_MPLANE) && CaptureSupportsH264(fd)) {
        is_h264_m2m = true;
      }
    }
    close(fd);

    if (is_h264_m2m) {
      RTC_LOG(LS_INFO) << "Found V4L2 H264 hardware encoder: " << path;
      return std::string(path);
    }
  }
  RTC_LOG(LS_INFO) << "No V4L2 H264 hardware encoder found";
  return std::string();
}

HardwareVideoEncoderFactory::HardwareVideoEncoderFactory(
    std::unique_ptr<webrtc::VideoEncoderFactory> inner)
    : inner_(std::move(inner)), h264_device_(FindV4L2H264EncoderDevice()) {}

std::vector<webrtc::SdpVideoFormat>
HardwareVideoEncoderFactory::GetSupportedFormats() const {
  // Advertise exactly what the inner (builtin) factory supports; the hardware
  // encoder produces standard H.264, so no format changes are needed.
  return inner_->GetSupportedFormats();
}

std::unique_ptr<webrtc::VideoEncoder>
HardwareVideoEncoderFactory::CreateVideoEncoder(
    const webrtc::SdpVideoFormat& format) {
  if (!h264_device_.empty() && format.name == kH264CodecName) {
    RTC_LOG(LS_INFO) << "Using V4L2 hardware H264 encoder on " << h264_device_;
    return std::make_unique<V4L2H264Encoder>(h264_device_);
  }
  return inner_->CreateVideoEncoder(format);
}

std::unique_ptr<webrtc::VideoEncoderFactory::EncoderSelectorInterface>
HardwareVideoEncoderFactory::GetEncoderSelector() const {
  return inner_->GetEncoderSelector();
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
