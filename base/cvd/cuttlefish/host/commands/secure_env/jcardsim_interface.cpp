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

// Stub implementation — JNI/JVM not available in this build.
// The real implementation lives in AOSP and requires libjvm + jcardsim.jar.

#include "cuttlefish/host/commands/secure_env/jcardsim_interface.h"

namespace cuttlefish {

Result<std::unique_ptr<JCardSimInterface>> JCardSimInterface::Create() {
  return CF_ERR("JCardSimulator is not available (built without JNI support)");
}

Result<std::vector<uint8_t>> JCardSimInterface::Transmit(const uint8_t*,
                                                          size_t) const {
  return CF_ERR("JCardSimulator is not available (built without JNI support)");
}

}  // namespace cuttlefish
