#include "WatchdogWin.hpp"

// iOS suspends application threads aggressively while backgrounded and does
// not expose the Linux /proc and syscall interfaces used by WatchdogPosix.
// Keep the public hooks harmless on iOS; crash diagnostics are provided by
// Xcode/TrollStore rather than a signal-driven watchdog.
namespace th06::WatchdogWin
{
void Init() {}
void Shutdown() {}
void TickHeartbeat() {}
bool RequestManualDump() { return false; }
} // namespace th06::WatchdogWin
