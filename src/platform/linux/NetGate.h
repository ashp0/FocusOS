#pragma once

#include <QString>
#include <QStringList>

class NetGate
{
public:
    QString buildRuleset(const QStringList &allowedHosts) const;
    bool apply(const QStringList &allowedHosts, QString *errorMessage = nullptr) const;
    // Hard clamp: drop ALL egress (loopback only) — not even the allowlist or
    // DNS gets through. Used when the browser blocker extension is disabled
    // mid-session so there is no way to browse around the missing enforcer.
    bool applyFullDeny(QString *errorMessage = nullptr) const;
    void drop() const;
};

