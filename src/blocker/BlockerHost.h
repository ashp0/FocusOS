#pragma once

namespace focusos {

// Run the FocusOS binary as a Chrome/Brave native-messaging host. Speaks the
// stdio protocol the blocker extension expects (4-byte little-endian length
// prefix + UTF-8 JSON), reads the v5 policy from BlockerPolicy and pushes live
// updates whenever ~/.focusos/blocker/policy.json changes. Runs headless — no
// QApplication, no window, no instance lock. Returns a process exit code.
int runBlockerHost();

} // namespace focusos
