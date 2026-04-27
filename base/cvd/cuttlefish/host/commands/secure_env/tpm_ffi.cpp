//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tpm_ffi.h"

#include "absl/log/log.h"
#include "absl/log/check.h"

#include "cuttlefish/host/commands/secure_env/tpm_hmac.h"
#include "cuttlefish/host/commands/secure_env/tpm_resource_manager.h"

using cuttlefish::TpmResourceManager;

extern "C" {

uint32_t tpm_hmac(void* trm, const uint8_t* data, uint32_t data_len,
                  uint8_t* tag, uint32_t tag_len) {
  if (trm == nullptr) {
    LOG(ERROR) << "No TPM resource manager provided";
    return 1;
  }
  TpmResourceManager* resource_manager =
      reinterpret_cast<TpmResourceManager*>(trm);
  auto hmac =
      TpmHmacWithContext(*resource_manager, "TpmHmac_context", data, data_len);
  if (!hmac) {
    LOG(ERROR) << "Could not calculate HMAC";
    return 1;
  } else if (hmac->size != tag_len) {
    LOG(ERROR) << "HMAC size of " << hmac->size
               << " different than expected tag len " << tag_len;
    return 1;
  }
  memcpy(tag, hmac->buffer, tag_len);
  return 0;
}

void secure_env_log(const char* file, unsigned int line, int severity,
                    const char* tag, const char* msg) {
  // Severity mapping from Rust log crate: 0=VERBOSE, 1=DEBUG, 2=INFO,
  // 3=WARNING, 4=ERROR, 5=FATAL_WITHOUT_ABORT, 6=FATAL
  switch (severity) {
    case 0:  // VERBOSE
      VLOG(1) << "[" << tag << "] " << msg;
      break;
    case 1:  // DEBUG
      VLOG(0) << "[" << tag << "] " << msg;
      break;
    case 2:  // INFO
      LOG(INFO) << "[" << tag << "] " << msg;
      break;
    case 3:  // WARNING
      LOG(WARNING) << "[" << tag << "] " << msg;
      break;
    default:
    case 4:  // ERROR
      LOG(ERROR) << "[" << tag << "] " << msg;
      break;
    case 5:  // FATAL_WITHOUT_ABORT
      LOG(ERROR) << "[FATAL] [" << tag << "] " << msg;
      break;
    case 6:  // FATAL
      LOG(FATAL) << "[" << tag << "] " << msg;
      break;
  }
}
}
