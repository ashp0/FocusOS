#pragma once

#include <QString>
#include <QStringList>

class NetGate
{
public:
    QString buildRuleset(const QStringList &allowedHosts) const;
    bool apply(const QStringList &allowedHosts, QString *errorMessage = nullptr) const;
    void drop() const;
};

