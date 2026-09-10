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

#include "cuttlefish/host/frontend/webrtc/libcommon/hwenc_v4l2/v4l2_h264_encode_converter.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>

// Linux
#include <fcntl.h>
#include <sys/ioctl.h>

// WebRTC
#include <api/video/i420_buffer.h>
#include <api/video/video_frame_buffer.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/logging.h>
#include <rtc_base/time_utils.h>

// libyuv (exposed by the @libyuv external as a top-level header)
#include "libyuv.h"

namespace {
// Number of V4L2 planes for an encoder input pixel format.
int InputNumPlanes(uint32_t fourcc) {
  switch (fourcc) {
    case V4L2_PIX_FMT_YUV420M:
      return 3;  // Y, U, V in separate planes
    case V4L2_PIX_FMT_NV12M:
      return 2;  // Y plane, interleaved UV plane
    default:
      return 1;  // NV12 / YUV420: one contiguous plane
  }
}
}  // namespace

// V4L2Helper

void V4L2Helper::InitFormat(int type,
                            int width,
                            int height,
                            int pixelformat,
                            int bytesperline,
                            int sizeimage,
                            v4l2_format* fmt) {
  fmt->type = type;
  fmt->fmt.pix_mp.width = width;
  fmt->fmt.pix_mp.height = height;
  fmt->fmt.pix_mp.pixelformat = pixelformat;
  fmt->fmt.pix_mp.field = V4L2_FIELD_ANY;
  fmt->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
  fmt->fmt.pix_mp.num_planes = 1;
  fmt->fmt.pix_mp.plane_fmt[0].bytesperline = bytesperline;
  fmt->fmt.pix_mp.plane_fmt[0].sizeimage = sizeimage;
}

int V4L2Helper::QueueBuffers(int fd, const V4L2Buffers& buffers) {
  for (int i = 0; i < buffers.count(); i++) {
    v4l2_plane planes[VIDEO_MAX_PLANES];
    v4l2_buffer v4l2_buf = {};
    v4l2_buf.type = buffers.type();
    v4l2_buf.memory = buffers.memory();
    v4l2_buf.index = i;
    v4l2_buf.length = 1;
    v4l2_buf.m.planes = planes;
    if (ioctl(fd, VIDIOC_QBUF, &v4l2_buf) < 0) {
      RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to queue buffer"
                        << " type=" << buffers.type()
                        << " memory=" << buffers.memory() << " index=" << i
                        << " error=" << strerror(errno);
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

// V4L2H264EncodeConverter
std::shared_ptr<V4L2H264EncodeConverter> V4L2H264EncodeConverter::Create(
    std::string device,
    int h264_profile,
    uint32_t input_format,
    int src_memory,
    int src_width,
    int src_height,
    int src_stride) {
  auto p = std::make_shared<V4L2H264EncodeConverter>();
  if (p->Init(device, h264_profile, input_format, src_memory, src_width,
              src_height, src_stride) != WEBRTC_VIDEO_CODEC_OK) {
    return nullptr;
  }
  return p;
}

int V4L2H264EncodeConverter::Init(std::string device,
                                  int h264_profile,
                                  uint32_t input_format,
                                  int src_memory,
                                  int src_width,
                                  int src_height,
                                  int src_stride) {
  input_format_ = input_format;
  // Non-blocking so the poll thread's VIDIOC_DQBUF returns EAGAIN instead of
  // blocking when a queue has nothing ready; a blocking dequeue would hang the
  // poll thread at teardown (no more frames arrive), deadlocking the join in
  // the destructor before it can close the device.
  fd_ = open(device.c_str(), O_RDWR | O_NONBLOCK, 0);
  if (fd_ < 0) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to open v4l2 encoder "
                      << device;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  v4l2_control ctrl = {};
  ctrl.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
  // The factory probed this profile from the device's supported set.
  ctrl.value = h264_profile;
  if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to set profile";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  ctrl.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
  // 3.1 matches the advertised profile-level-id and fits 720x1280.
  ctrl.value = V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
  if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to set level";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Optional tuning controls, non-fatal when unsupported: keyframes are
  // requested on demand over RTCP rather than a fixed intra period, and when
  // the encoder won't inline SPS/PPS the caller prepends them in software.
  ctrl.id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
  ctrl.value = 500;
  if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    RTC_LOG(LS_WARNING) << __FUNCTION__ << "  intra period not supported";
  }

  ctrl.id = V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER;
  ctrl.value = 1;
  if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    RTC_LOG(LS_WARNING) << __FUNCTION__ << "  inline headers not supported";
  }

  // Encoder input: the planar/semi-planar YUV 4:2:0 format the device accepts.
  const int y_size = src_width * src_height;
  const int c_size = (src_width / 2) * (src_height / 2);
  v4l2_format src_fmt = {};
  src_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  src_fmt.fmt.pix_mp.width = src_width;
  src_fmt.fmt.pix_mp.height = src_height;
  src_fmt.fmt.pix_mp.pixelformat = input_format_;
  src_fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
  src_fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
  src_fmt.fmt.pix_mp.num_planes = InputNumPlanes(input_format_);
  src_fmt.fmt.pix_mp.plane_fmt[0].bytesperline = src_width;
  if (input_format_ == V4L2_PIX_FMT_YUV420M) {
    src_fmt.fmt.pix_mp.plane_fmt[0].sizeimage = y_size;
    src_fmt.fmt.pix_mp.plane_fmt[1].bytesperline = src_width / 2;
    src_fmt.fmt.pix_mp.plane_fmt[1].sizeimage = c_size;
    src_fmt.fmt.pix_mp.plane_fmt[2].bytesperline = src_width / 2;
    src_fmt.fmt.pix_mp.plane_fmt[2].sizeimage = c_size;
  } else if (input_format_ == V4L2_PIX_FMT_NV12M) {
    src_fmt.fmt.pix_mp.plane_fmt[0].sizeimage = y_size;
    src_fmt.fmt.pix_mp.plane_fmt[1].bytesperline = src_width;
    src_fmt.fmt.pix_mp.plane_fmt[1].sizeimage = 2 * c_size;  // interleaved UV
  } else {  // NV12 or YUV420: a single contiguous plane
    src_fmt.fmt.pix_mp.plane_fmt[0].sizeimage = y_size + 2 * c_size;
  }
  if (ioctl(fd_, VIDIOC_S_FMT, &src_fmt) < 0) {
    RTC_LOG(LS_ERROR) << "Failed to set output format";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  RTC_LOG(LS_INFO) << __FUNCTION__ << "  Output buffer format"
                   << "  width:" << src_fmt.fmt.pix_mp.width
                   << "  height:" << src_fmt.fmt.pix_mp.height
                   << "  planes:" << (int)src_fmt.fmt.pix_mp.num_planes;

  v4l2_format dst_fmt = {};
  V4L2Helper::InitFormat(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, src_width,
                         src_height, V4L2_PIX_FMT_H264, 0, 512 << 10, &dst_fmt);
  if (ioctl(fd_, VIDIOC_S_FMT, &dst_fmt) < 0) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to set capture format";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  int r =
      src_buffers_.Allocate(fd_, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, src_memory,
                            NUM_OUTPUT_BUFFERS, &src_fmt, false);
  if (r != WEBRTC_VIDEO_CODEC_OK) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to allocate output buffers";
    return r;
  }

  r = dst_buffers_.Allocate(fd_, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                            V4L2_MEMORY_MMAP, NUM_CAPTURE_BUFFERS, &dst_fmt,
                            false);
  if (r != WEBRTC_VIDEO_CODEC_OK) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to request output buffers";
    return r;
  }

  r = V4L2Helper::QueueBuffers(fd_, dst_buffers_);
  if (r != WEBRTC_VIDEO_CODEC_OK) {
    return r;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

int V4L2H264EncodeConverter::fd() const {
  return fd_;
}

int V4L2H264EncodeConverter::Encode(
    const rtc::scoped_refptr<webrtc::VideoFrameBuffer>& frame_buffer,
    int64_t timestamp_us,
    bool force_key_frame,
    OnCompleteCallback on_complete) {
  if (force_key_frame) {
    v4l2_control ctrl = {};
    ctrl.id = V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME;
    ctrl.value = 1;
    if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
      RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to request I frame";
    }
  }

  if (!runner_) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
      RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to start output stream";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
      RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to start capture stream";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }

    runner_ = V4L2Runner::Create("H264Encoder", fd_, src_buffers_.count(),
                                 src_buffers_.memory(), V4L2_MEMORY_MMAP);
  }

  std::optional<int> index = runner_->PopAvailableBufferIndex();
  if (!index) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "  No available output buffers";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  v4l2_buffer v4l2_buf = {};
  v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  v4l2_buf.memory = V4L2_MEMORY_MMAP;
  v4l2_buf.index = *index;
  v4l2_buf.field = V4L2_FIELD_NONE;
  v4l2_buf.length = InputNumPlanes(input_format_);
  v4l2_plane planes[VIDEO_MAX_PLANES] = {};
  v4l2_buf.m.planes = planes;
  v4l2_buf.flags |= V4L2_BUF_FLAG_TIMESTAMP_COPY;
  v4l2_buf.timestamp.tv_sec = timestamp_us / rtc::kNumMicrosecsPerSec;
  v4l2_buf.timestamp.tv_usec = timestamp_us % rtc::kNumMicrosecsPerSec;

  // Cuttlefish feeds CPU-side I420 frames; convert them into the MMAP output
  // buffer laid out in the encoder's input pixel format.
  auto& src_buffer = src_buffers_.at(v4l2_buf.index);
  rtc::scoped_refptr<webrtc::I420BufferInterface> i420_buffer =
      frame_buffer->ToI420();
  int width = i420_buffer->width();
  int height = i420_buffer->height();
  uint8_t* dst = (uint8_t*)src_buffer.planes[0].start;
  int y_stride = src_buffer.planes[0].bytesperline;
  if (input_format_ == V4L2_PIX_FMT_YUV420M) {
    libyuv::I420Copy(i420_buffer->DataY(), i420_buffer->StrideY(),
                     i420_buffer->DataU(), i420_buffer->StrideU(),
                     i420_buffer->DataV(), i420_buffer->StrideV(), dst, y_stride,
                     (uint8_t*)src_buffer.planes[1].start,
                     src_buffer.planes[1].bytesperline,
                     (uint8_t*)src_buffer.planes[2].start,
                     src_buffer.planes[2].bytesperline, width, height);
  } else if (input_format_ == V4L2_PIX_FMT_NV12M) {
    libyuv::I420ToNV12(i420_buffer->DataY(), i420_buffer->StrideY(),
                       i420_buffer->DataU(), i420_buffer->StrideU(),
                       i420_buffer->DataV(), i420_buffer->StrideV(), dst,
                       y_stride, (uint8_t*)src_buffer.planes[1].start,
                       src_buffer.planes[1].bytesperline, width, height);
  } else if (input_format_ == V4L2_PIX_FMT_NV12) {
    // Single plane: Y followed by interleaved UV.
    libyuv::I420ToNV12(i420_buffer->DataY(), i420_buffer->StrideY(),
                       i420_buffer->DataU(), i420_buffer->StrideU(),
                       i420_buffer->DataV(), i420_buffer->StrideV(), dst,
                       y_stride, dst + y_stride * height, y_stride, width,
                       height);
  } else {  // V4L2_PIX_FMT_YUV420: single plane, Y then U then V.
    int c_stride = y_stride / 2;
    uint8_t* u = dst + y_stride * height;
    uint8_t* v = u + c_stride * (height / 2);
    libyuv::I420Copy(i420_buffer->DataY(), i420_buffer->StrideY(),
                     i420_buffer->DataU(), i420_buffer->StrideU(),
                     i420_buffer->DataV(), i420_buffer->StrideV(), dst, y_stride,
                     u, c_stride, v, c_stride, width, height);
  }
  for (int j = 0; j < InputNumPlanes(input_format_); j++) {
    planes[j].bytesused = src_buffer.planes[j].sizeimage;
    planes[j].length = src_buffer.planes[j].length;
  }

  runner_->Enqueue(
      &v4l2_buf, [this, on_complete](
                     v4l2_buffer* v4l2_buf, std::function<void()> on_next) {
        int64_t timestamp_us =
            v4l2_buf->timestamp.tv_sec * rtc::kNumMicrosecsPerSec +
            v4l2_buf->timestamp.tv_usec;
        bool is_key_frame = !!(v4l2_buf->flags & V4L2_BUF_FLAG_KEYFRAME);
        V4L2Buffers::PlaneBuffer& plane =
            dst_buffers_.at(v4l2_buf->index).planes[0];
        on_complete((uint8_t*)plane.start, v4l2_buf->m.planes[0].bytesused,
                    timestamp_us, is_key_frame);
        on_next();
      });

  return WEBRTC_VIDEO_CODEC_OK;
}

V4L2H264EncodeConverter::~V4L2H264EncodeConverter() {
  runner_.reset();

  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  if (ioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to stop output stream";
  }
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  if (ioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "  Failed to stop capture stream";
  }

  src_buffers_.Deallocate();
  dst_buffers_.Deallocate();

  close(fd_);
}
