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
 * Derived from shiguredo/momo src/hwenc_v4l2/v4l2_converter (Apache-2.0),
 * trimmed to the H.264 encode path and parameterized on the device node.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

// Linux
#include <linux/videodev2.h>

// WebRTC
#include <api/scoped_refptr.h>
#include <api/video/video_frame_buffer.h>

#include "cuttlefish/host/frontend/webrtc/libcommon/hwenc_v4l2/v4l2_buffers.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/hwenc_v4l2/v4l2_runner.h"

class V4L2Helper {
 public:
  static void InitFormat(int type,
                         int width,
                         int height,
                         int pixelformat,
                         int bytesperline,
                         int sizeimage,
                         v4l2_format* fmt);
  static int QueueBuffers(int fd, const V4L2Buffers& buffers);
};

// Encodes I420 frames to H.264 via a V4L2 memory-to-memory encoder.
class V4L2H264EncodeConverter {
 public:
  typedef std::function<void(uint8_t*, int, int64_t, bool)> OnCompleteCallback;

  // |device| is the V4L2 m2m encoder node, e.g. "/dev/video11".
  static std::shared_ptr<V4L2H264EncodeConverter> Create(std::string device,
                                                         int src_memory,
                                                         int src_width,
                                                         int src_height,
                                                         int src_stride);

 private:
  static constexpr int NUM_OUTPUT_BUFFERS = 4;
  static constexpr int NUM_CAPTURE_BUFFERS = 4;

  int Init(std::string device,
           int src_memory,
           int src_width,
           int src_height,
           int src_stride);

 public:
  int fd() const;

  int Encode(const rtc::scoped_refptr<webrtc::VideoFrameBuffer>& frame_buffer,
             int64_t timestamp_us,
             bool force_key_frame,
             OnCompleteCallback on_complete);

  ~V4L2H264EncodeConverter();

 private:
  int fd_ = 0;

  V4L2Buffers src_buffers_;
  V4L2Buffers dst_buffers_;

  std::shared_ptr<V4L2Runner> runner_;
};
