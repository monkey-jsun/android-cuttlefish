// Stub implementation of kmr_ta_main for Phase 1 (C++ only build).
// Phase 2 will replace this with the real Rust KeyMint TA implementation.

#include "cuttlefish/host/commands/secure_env/rust/kmr_ta.h"

#include "absl/log/log.h"
#include "absl/log/check.h"

void kmr_ta_main(int fd_in, int fd_out, int security_level, void* trm,
                 int snapshot_socket_fd) {
  LOG(WARNING) << "Rust KeyMint TA not available (stub build). "
               << "KeyMint requests on this channel will not be processed. "
               << "C++ Keymaster implementation is still active.";
  // Block forever — the thread will just sit idle.
  // The C++ keymaster thread handles keymaster requests.
  pause();
}
