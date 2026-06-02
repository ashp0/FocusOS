#pragma once

#include <QString>
#include <QStringList>

class NetGate
{
public:
    // Resolve the allowlist (DNS — the SLOW part) and render the nftables ruleset
    // text. Safe to call from a worker thread: it only does DNS + string building
    // and touches no shared state. Pair with applyRuleset() on the main thread.
    QString buildRuleset(const QStringList &allowedHosts) const;
    // Load a ruleset produced by buildRuleset() into the kernel via `nft -f -`.
    // Fast (no DNS); runs the actual privileged netfilter swap. Main-thread.
    bool applyRuleset(const QString &ruleset, QString *errorMessage = nullptr) const;
    // Convenience: buildRuleset() + applyRuleset() in one synchronous call. Keep
    // for paths where blocking is acceptable; the async engage path splits them.
    bool apply(const QStringList &allowedHosts, QString *errorMessage = nullptr) const;
    // Hard clamp: drop ALL egress (loopback only) — not even the allowlist or
    // DNS gets through. Used when the browser blocker extension is disabled
    // mid-session so there is no way to browse around the missing enforcer.
    bool applyFullDeny(QString *errorMessage = nullptr) const;
    void drop() const;
};

