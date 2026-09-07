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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_encoder.h>
#include <api/video_codecs/video_encoder_factory.h>

namespace cuttlefish {
namespace webrtc_streaming {

// Returns the path of a V4L2 memory-to-memory H.264 encoder node (e.g.
// "/dev/video11"), or an empty string if the host has none. Used both to pick
// the offered codec at startup and to route H.264 encoding to hardware.
std::string FindV4L2H264EncoderDevice();

// Wraps an inner (builtin, software) encoder factory and transparently routes
// H.264 to the host's V4L2 hardware encoder when one is present. Every other
// codec -- and H.264 when no hardware encoder exists -- falls through to the
// inner factory unchanged.
class HardwareVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  explicit HardwareVideoEncoderFactory(
      std::unique_ptr<webrtc::VideoEncoderFactory> inner);

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(
      const webrtc::SdpVideoFormat& format) override;

  std::unique_ptr<EncoderSelectorInterface> GetEncoderSelector() const override;

 private:
  std::unique_ptr<webrtc::VideoEncoderFactory> inner_;
  std::string h264_device_;  // empty when no hardware H.264 encoder is present
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
