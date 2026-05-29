#pragma once

#include <QString>
#include <QStringList>

// Shared state file between the FocusOS process and the browser-spawned native
// host. The main process writes it (via the platform backends when a routine's
// network lock is applied/dropped); the host process reads + watches it and
// pushes the corresponding v5 policy to the blocker extension.
//
// On disk: ~/.focusos/blocker/policy.dat — a signed binary blob (NOT a plain
// text/JSON file a user can edit):
//   "FOSB" magic + version byte + QDataStream{active, allowlist, issuedAt}
//   + HMAC-SHA256 over all of the above (key from BlockerSecret).
//
//   active=true  -> block everything except `allowlist`
//   active=false / absent -> allow everything (no focus session)
//
// Fail-closed: a missing or tampered file while a routine is live
// (~/.focusos/active.json shows remaining time) reads back as "block
// everything", so editing/deleting the file can't escape a lock.
namespace BlockerPolicy {

struct State
{
    bool active = false;
    QStringList allowlist;
};

// Absolute path to ~/.focusos/blocker (created if missing).
QString blockerDir();
QString policyFilePath();

// ~/.focusos/blocker/host-alive — a liveness beacon the native host rewrites
// every ~1.5s while a browser is connected to it (i.e. while the blocker
// extension is enabled and talking to us). The main process reads its mtime to
// tell whether the extension is actually enforcing; a stale/missing file while
// a browser is running means the extension was disabled or removed.
QString heartbeatFilePath();

// Atomic write (temp + rename) of the signed blob.
void write(bool active, const QStringList &allowlist);

// Verified read. On a missing/tampered/corrupt file, falls back to the
// fail-closed rule above (block-all during a live session, else allow-all).
State read();

// True if ~/.focusos/active.json describes a live routine (remaining > 0).
bool sessionCheckpointActive();

// Strip scheme / leading `www.` / trailing slash, lowercase. Turns the full
// URLs that may live in routine.allowedUrls into the bare hosts the extension
// matches on. Empty if nothing useful remains.
QString normalizeHost(const QString &raw);

} // namespace BlockerPolicy
