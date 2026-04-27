/*
 * Copyright 2024 The Android Open Source Project
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
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Stub interface for JCardSimulator.  The real implementation requires JNI
// (libjvm.so + jcardsim.jar) which is not available in this build.
// secure_env_not_windows_main.cpp gates all jcardsim usage behind
// FLAGS_enable_jcard_simulator, so this stub is never called at runtime.
class JCardSimInterface {
 public:
  ~JCardSimInterface() = default;

  static Result<std::unique_ptr<JCardSimInterface>> Create();

  Result<std::vector<uint8_t>> Transmit(const uint8_t* data, size_t len) const;
};

}  // namespace cuttlefish
