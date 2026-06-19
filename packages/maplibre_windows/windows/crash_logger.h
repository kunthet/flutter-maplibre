#pragma once

namespace maplibre_windows {

// Installs a process-wide unhandled-exception handler that writes a
// module-level stack trace of the faulting thread to a log file
// (%TEMP%\maplibre_crash.log). Diagnostic aid for the ANGLE render crash; safe
// to leave installed (no effect unless the process crashes).
void InstallCrashLogger();

}  // namespace maplibre_windows
