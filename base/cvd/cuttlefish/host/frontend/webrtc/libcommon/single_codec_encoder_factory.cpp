/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "cuttlefish/host/frontend/webrtc/libcommon/single_codec_encoder_factory.h"

#include <api/video_codecs/video_encoder.h>

namespace cuttlefish {
namespace webrtc_streaming {
SingleCodecEncoderFactory::SingleCodecEncoderFactory(
    std::unique_ptr<webrtc::VideoEncoderFactory> inner, std::string codec_name)
    : inner_(std::move(inner)), codec_name_(std::move(codec_name)) {}

std::vector<webrtc::SdpVideoFormat>
SingleCodecEncoderFactory::GetSupportedFormats() const {
  std::vector<webrtc::SdpVideoFormat> ret;
  for (auto& format : inner_->GetSupportedFormats()) {
    if (format.name == codec_name_) {
      ret.push_back(format);
    }
  }
  return ret;
}

std::unique_ptr<webrtc::VideoEncoder>
SingleCodecEncoderFactory::CreateVideoEncoder(
    const webrtc::SdpVideoFormat& format) {
  return inner_->CreateVideoEncoder(format);
}

std::unique_ptr<webrtc::VideoEncoderFactory::EncoderSelectorInterface>
SingleCodecEncoderFactory::GetEncoderSelector() const {
  return inner_->GetEncoderSelector();
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
