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

#include <algorithm>
#include <cstdio>
#include <cstring>

// Linux
#include <linux/videodev2.h>
#include <sys/ioctl.h>

// WebRTC
#include <api/video_codecs/h264_profile_level_id.h>
#include <rtc_base/logging.h>

#include "cuttlefish/host/frontend/webrtc/libcommon/hwenc_v4l2/v4l2_h264_encoder.h"

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

constexpr char kH264CodecName[] = "H264";

// Pick the H.264 profile to encode with, preferring the most broadly
// decodable one the device offers. Returns the V4L2 profile value and, via
// |sdp_profile|, the profile to advertise in SDP (a hardware Baseline stream
// is constrained-baseline compatible, so it is advertised as such).
int ProbeBestH264Profile(int fd, webrtc::H264Profile* sdp_profile) {
  struct Pref {
    int v4l2;
    webrtc::H264Profile sdp;
  };
  const Pref prefs[] = {
      {V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE,
       webrtc::H264Profile::kProfileConstrainedBaseline},
      {V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
       webrtc::H264Profile::kProfileConstrainedBaseline},
      {V4L2_MPEG_VIDEO_H264_PROFILE_MAIN, webrtc::H264Profile::kProfileMain},
      {V4L2_MPEG_VIDEO_H264_PROFILE_HIGH, webrtc::H264Profile::kProfileHigh},
  };
  for (const Pref& p : prefs) {
    v4l2_querymenu qm = {};
    qm.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
    qm.index = p.v4l2;
    if (ioctl(fd, VIDIOC_QUERYMENU, &qm) == 0) {
      *sdp_profile = p.sdp;
      return p.v4l2;
    }
  }
  // Driver exposes no profile menu; Baseline is the safe universal default.
  *sdp_profile = webrtc::H264Profile::kProfileConstrainedBaseline;
  return V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
}

// Pick the encoder input pixel format, preferring CPU-friendly planar /
// semi-planar 4:2:0 layouts the device enumerates on its input (OUTPUT) queue.
uint32_t ProbeBestInputFormat(int fd) {
  const uint32_t prefs[] = {V4L2_PIX_FMT_YUV420M, V4L2_PIX_FMT_NV12M,
                            V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_YUV420};
  for (uint32_t want : prefs) {
    for (int i = 0;; i++) {
      v4l2_fmtdesc desc = {};
      desc.index = i;
      desc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
      if (ioctl(fd, VIDIOC_ENUM_FMT, &desc) < 0) {
        break;  // no more formats
      }
      if (desc.pixelformat == want) {
        return want;
      }
    }
  }
  return V4L2_PIX_FMT_YUV420M;  // safe default for a 4:2:0 m2m encoder
}

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

// Confirm the device can actually be *configured* for encode, not merely that
// it enumerates H.264: set the profile and formats the encoder will use and
// reserve buffers on both queues. A device that advertises H.264 but rejects
// this setup (a non-conforming encoder) fails here, so `auto` falls back to VP8
// rather than negotiating H.264 and then black-screening.
bool VerifyEncoderUsable(int fd, int profile, uint32_t input_format) {
  v4l2_control ctrl = {};
  ctrl.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
  ctrl.value = profile;
  if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
    return false;
  }

  const int w = 640, h = 480;
  const int y = w * h, c = (w / 2) * (h / 2);
  v4l2_format ofmt = {};
  ofmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  ofmt.fmt.pix_mp.width = w;
  ofmt.fmt.pix_mp.height = h;
  ofmt.fmt.pix_mp.pixelformat = input_format;
  ofmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
  ofmt.fmt.pix_mp.plane_fmt[0].bytesperline = w;
  if (input_format == V4L2_PIX_FMT_YUV420M) {
    ofmt.fmt.pix_mp.num_planes = 3;
    ofmt.fmt.pix_mp.plane_fmt[0].sizeimage = y;
    ofmt.fmt.pix_mp.plane_fmt[1].bytesperline = w / 2;
    ofmt.fmt.pix_mp.plane_fmt[1].sizeimage = c;
    ofmt.fmt.pix_mp.plane_fmt[2].bytesperline = w / 2;
    ofmt.fmt.pix_mp.plane_fmt[2].sizeimage = c;
  } else if (input_format == V4L2_PIX_FMT_NV12M) {
    ofmt.fmt.pix_mp.num_planes = 2;
    ofmt.fmt.pix_mp.plane_fmt[0].sizeimage = y;
    ofmt.fmt.pix_mp.plane_fmt[1].bytesperline = w;
    ofmt.fmt.pix_mp.plane_fmt[1].sizeimage = 2 * c;
  } else {
    ofmt.fmt.pix_mp.num_planes = 1;
    ofmt.fmt.pix_mp.plane_fmt[0].sizeimage = y + 2 * c;
  }
  if (ioctl(fd, VIDIOC_S_FMT, &ofmt) < 0) {
    return false;
  }

  v4l2_format cfmt = {};
  cfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  cfmt.fmt.pix_mp.width = w;
  cfmt.fmt.pix_mp.height = h;
  cfmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
  cfmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
  cfmt.fmt.pix_mp.num_planes = 1;
  cfmt.fmt.pix_mp.plane_fmt[0].sizeimage = 256 << 10;
  if (ioctl(fd, VIDIOC_S_FMT, &cfmt) < 0) {
    return false;
  }

  // Reserve then immediately release a buffer on each queue.
  const int types[] = {V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                       V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
  for (int type : types) {
    v4l2_requestbuffers rb = {};
    rb.count = 1;
    rb.type = type;
    rb.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) {
      return false;
    }
    rb.count = 0;
    ioctl(fd, VIDIOC_REQBUFS, &rb);  // free
  }
  return true;
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
    // Accept the device only if it also accepts the encode configuration, so a
    // present-but-non-conforming encoder doesn't get H.264 offered to it.
    bool usable = false;
    if (is_h264_m2m) {
      webrtc::H264Profile sdp_profile;
      int profile = ProbeBestH264Profile(fd, &sdp_profile);
      uint32_t input_format = ProbeBestInputFormat(fd);
      usable = VerifyEncoderUsable(fd, profile, input_format);
      if (!usable) {
        RTC_LOG(LS_INFO) << path << " advertises H.264 but rejected the encode "
                            "setup; skipping";
      }
    }
    close(fd);

    if (usable) {
      RTC_LOG(LS_INFO) << "Found V4L2 H264 hardware encoder: " << path;
      return std::string(path);
    }
  }
  RTC_LOG(LS_INFO) << "No V4L2 H264 hardware encoder found";
  return std::string();
}

HardwareVideoEncoderFactory::HardwareVideoEncoderFactory(
    std::unique_ptr<webrtc::VideoEncoderFactory> inner)
    : inner_(std::move(inner)),
      h264_device_(FindV4L2H264EncoderDevice()),
      h264_profile_(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE),
      h264_input_format_(V4L2_PIX_FMT_YUV420M),
      h264_profile_level_id_("42e01f") {
  if (h264_device_.empty()) {
    return;
  }
  int fd = open(h264_device_.c_str(), O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    return;
  }
  webrtc::H264Profile sdp_profile;
  h264_profile_ = ProbeBestH264Profile(fd, &sdp_profile);
  h264_input_format_ = ProbeBestInputFormat(fd);
  close(fd);
  // Advertise a level (3.1) covering cuttlefish's default display; the encoder
  // is configured to the same level.
  auto plid = webrtc::H264ProfileLevelIdToString(
      webrtc::H264ProfileLevelId(sdp_profile, webrtc::H264Level::kLevel3_1));
  if (plid) {
    h264_profile_level_id_ = *plid;
  }
  char fourcc[5] = {static_cast<char>(h264_input_format_ & 0xff),
                    static_cast<char>((h264_input_format_ >> 8) & 0xff),
                    static_cast<char>((h264_input_format_ >> 16) & 0xff),
                    static_cast<char>((h264_input_format_ >> 24) & 0xff), 0};
  RTC_LOG(LS_INFO) << "V4L2 H264 encoder: profile-level-id="
                   << h264_profile_level_id_ << " input=" << fourcc;
}

std::vector<webrtc::SdpVideoFormat>
HardwareVideoEncoderFactory::GetSupportedFormats() const {
  auto formats = inner_->GetSupportedFormats();
  if (!h264_device_.empty()) {
    // The builtin software factory usually has no H.264 encoder, so it never
    // advertises H.264 -- which leaves the offer with an empty video codec set
    // when H.264 is selected. Advertise it ourselves since the V4L2 hardware
    // encoder provides it, at the profile the device was probed to support,
    // in both packetization modes, which browsers accept.
    for (const char* packetization_mode : {"1", "0"}) {
      webrtc::SdpVideoFormat h264(
          kH264CodecName,
          {{"level-asymmetry-allowed", "1"},
           {"packetization-mode", packetization_mode},
           {"profile-level-id", h264_profile_level_id_}});
      if (std::find(formats.begin(), formats.end(), h264) == formats.end()) {
        formats.push_back(h264);
      }
    }
  }
  return formats;
}

std::unique_ptr<webrtc::VideoEncoder>
HardwareVideoEncoderFactory::CreateVideoEncoder(
    const webrtc::SdpVideoFormat& format) {
  if (!h264_device_.empty() && format.name == kH264CodecName) {
    RTC_LOG(LS_INFO) << "Using V4L2 hardware H264 encoder on " << h264_device_;
    return std::make_unique<V4L2H264Encoder>(h264_device_, h264_profile_,
                                             h264_input_format_);
  }
  return inner_->CreateVideoEncoder(format);
}

std::unique_ptr<webrtc::VideoEncoderFactory::EncoderSelectorInterface>
HardwareVideoEncoderFactory::GetEncoderSelector() const {
  return inner_->GetEncoderSelector();
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
