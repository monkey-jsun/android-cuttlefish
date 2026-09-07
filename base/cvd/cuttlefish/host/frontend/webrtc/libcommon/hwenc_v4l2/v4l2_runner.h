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

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

// Linux
#include <linux/videodev2.h>

// WebRTC
#include <rtc_base/platform_thread.h>

template <class T>
class ConcurrentQueue {
 public:
  void push(T t) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(t);
  }
  std::optional<T> pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    T t = queue_.front();
    queue_.pop();
    return t;
  }
  bool empty() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
  }
  size_t size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_ = std::queue<T>();
  }

 private:
  std::queue<T> queue_;
  std::mutex mutex_;
};

// Runs the V4L2 m2m poll loop on a dedicated thread: dequeues finished
// output/capture buffers and hands the encoded capture buffer to the
// per-frame completion callback.
class V4L2Runner {
 public:
  ~V4L2Runner();

  static std::shared_ptr<V4L2Runner> Create(
      std::string name,
      int fd,
      int src_count,
      int src_memory,
      int dst_memory,
      std::function<void()> on_change_resolution = nullptr);

  typedef std::function<void(v4l2_buffer*, std::function<void()>)>
      OnCompleteCallback;

  int Enqueue(v4l2_buffer* v4l2_buf, OnCompleteCallback on_complete);

  std::optional<int> PopAvailableBufferIndex();

 private:
  void PollProcess();

 private:
  std::string name_;
  int fd_;
  int src_count_;
  int src_memory_;
  int dst_memory_;
  std::function<void()> on_change_resolution_;

  ConcurrentQueue<int> output_buffers_available_;
  ConcurrentQueue<OnCompleteCallback> on_completes_;
  std::atomic<bool> abort_poll_;
  rtc::PlatformThread thread_;
};
