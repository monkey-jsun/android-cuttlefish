/*
 * Copyright 2023 Shiguredo Inc.
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
 *
 * Derived from shiguredo/momo src/hwenc_v4l2/v4l2_h264_encoder (Apache-2.0),
 * trimmed to the I420 encode path and parameterized on the device node.
 */

#pragma once

#include <mutex>
#include <string>
#include <vector>

// WebRTC
#include <api/video_codecs/video_encoder.h>
#include <common_video/h264/h264_bitstream_parser.h>
#include <common_video/include/bitrate_adjuster.h>

#include "cuttlefish/host/frontend/webrtc/libcommon/hwenc_v4l2/v4l2_h264_encode_converter.h"

namespace cuttlefish {
namespace webrtc_streaming {

// A webrtc::VideoEncoder that encodes H.264 on a V4L2 m2m hardware encoder.
class V4L2H264Encoder : public webrtc::VideoEncoder {
 public:
  explicit V4L2H264Encoder(std::string device);
  ~V4L2H264Encoder() override;

  int32_t InitEncode(const webrtc::VideoCodec* codec_settings,
                     const webrtc::VideoEncoder::Settings& settings) override;
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  int32_t Release() override;
  void SetRates(const RateControlParameters& parameters) override;
  webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;
  int32_t Encode(
      const webrtc::VideoFrame& frame,
      const std::vector<webrtc::VideoFrameType>* frame_types) override;

 private:
  int32_t Configure(int32_t width, int32_t height);
  void SetBitrateBps(uint32_t bitrate_bps);
  void SetFramerateFps(double framerate_fps);
  int32_t SendFrame(const webrtc::VideoFrame& frame,
                    unsigned char* buffer,
                    size_t size,
                    int64_t timestamp_us,
                    bool is_key_frame);

 private:
  std::string device_;
  std::shared_ptr<V4L2H264EncodeConverter> h264_encoder_;

  int32_t configured_width_;
  int32_t configured_height_;

  webrtc::EncodedImageCallback* callback_;
  std::mutex callback_mutex_;
  webrtc::BitrateAdjuster bitrate_adjuster_;
  uint32_t target_bitrate_bps_;
  uint32_t configured_bitrate_bps_;
  double target_framerate_fps_;
  int32_t configured_framerate_fps_;

  webrtc::H264BitstreamParser h264_bitstream_parser_;

  webrtc::EncodedImage encoded_image_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
