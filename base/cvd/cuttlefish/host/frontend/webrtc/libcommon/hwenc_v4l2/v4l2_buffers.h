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
 * Derived from shiguredo/momo src/hwenc_v4l2 (Apache-2.0).
 */

#pragma once

#include <vector>

// Linux
#include <linux/videodev2.h>

// Holds a set of V4L2 buffers (MMAP or DMABUF) for one queue.
class V4L2Buffers {
 public:
  struct PlaneBuffer {
    void* start = nullptr;
    size_t length = 0;
    int sizeimage = 0;
    int bytesperline = 0;
    int fd = 0;
  };
  struct Buffer {
    PlaneBuffer planes[VIDEO_MAX_PLANES];
    size_t n_planes = 0;
  };

  int Allocate(int fd,
               int type,
               int memory,
               int req_count,
               v4l2_format* format,
               bool export_dmafds);

  void Deallocate();

  ~V4L2Buffers();

  int type() const;
  int memory() const;
  int count() const;
  bool dmafds_exported() const;
  Buffer& at(int index);

 private:
  int fd_ = 0;
  int type_ = 0;
  int memory_ = 0;
  bool export_dmafds_ = false;
  std::vector<Buffer> buffers_;
};
