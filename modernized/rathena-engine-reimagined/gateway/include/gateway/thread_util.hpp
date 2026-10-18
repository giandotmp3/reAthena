#pragma once
// Small OS helpers for gateway-owned threads.
#include <cerrno>

#if defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace gateway {

/// Lowers the scheduling priority (raises the nice value) of the CALLING thread by `nice` (0 = leave it).
/// Gateway workers run niced so the OS prefers the simulation thread when both want a core. Best effort:
/// returns false if the platform or the permissions do not allow it (never an error for the caller).
inline bool lower_this_thread_priority(int nice) {
#if defined(__linux__)
  if (nice <= 0) return false;
  return ::setpriority(PRIO_PROCESS, static_cast<id_t>(::syscall(SYS_gettid)), nice) == 0;
#else
  (void)nice;
  return false;
#endif
}

}  // namespace gateway
