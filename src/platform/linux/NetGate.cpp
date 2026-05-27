#include "platform/linux/NetGate.h"

#ifdef Q_OS_LINUX
#include <QAbstractSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QProcess>
#include <QSet>
#include <QUrl>
#endif

QString NetGate::buildRuleset(const QStringList &allowedHosts) const
{
#ifdef Q_OS_LINUX
    QSet<QString> resolvedAddresses;
    for (const QString &entry : allowedHosts) {
        QString host = entry.trimmed().toLower();
        if (host.isEmpty()) {
            continue;
        }

        const QUrl url = QUrl::fromUserInput(host);
        if (!url.host().isEmpty()) {
            host = url.host().toLower();
        } else {
            host = host.section(QLatin1Char('/'), 0, 0).section(QLatin1Char(':'), 0, 0);
        }

        const QHostAddress literalAddress(host);
        if (!literalAddress.isNull() && literalAddress.protocol() == QAbstractSocket::IPv4Protocol) {
            resolvedAddresses.insert(literalAddress.toString());
            continue;
        }

        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress &address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                resolvedAddresses.insert(address.toString());
            }
        }
    }
#endif

    QString rules;
    rules += QStringLiteral("table inet focusos {\n");
    rules += QStringLiteral("  set allowed_hosts {\n");
    rules += QStringLiteral("    type ipv4_addr\n");
    rules += QStringLiteral("    flags interval\n");
#ifdef Q_OS_LINUX
    if (!resolvedAddresses.isEmpty()) {
        QStringList sortedAddresses;
        sortedAddresses.reserve(resolvedAddresses.size());
        for (const QString &address : resolvedAddresses) {
            sortedAddresses.append(address);
        }
        sortedAddresses.sort();
        rules += QStringLiteral("    elements = { %1 }\n").arg(sortedAddresses.join(QStringLiteral(", ")));
    }
#endif
    rules += QStringLiteral("  }\n");
    rules += QStringLiteral("  chain output {\n");
    rules += QStringLiteral("    type filter hook output priority 0; policy drop;\n");
    rules += QStringLiteral("    oifname \"lo\" accept\n");
    rules += QStringLiteral("    meta l4proto { tcp, udp } th dport 53 accept\n");
    rules += QStringLiteral("    ip daddr @allowed_hosts accept\n");
    rules += QStringLiteral("  }\n");
    rules += QStringLiteral("}\n");
    rules += QStringLiteral("# Host allowlist resolved by the Linux backend:\n");
    for (const QString &host : allowedHosts) {
        rules += QStringLiteral("#   %1\n").arg(host);
    }
    return rules;
}

bool NetGate::apply(const QStringList &allowedHosts, QString *errorMessage) const
{
#ifdef Q_OS_LINUX
    QProcess nft;
    nft.start(QStringLiteral("nft"), {QStringLiteral("-f"), QStringLiteral("-")});
    if (!nft.waitForStarted()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to start nft");
        }
        return false;
    }
    nft.write(buildRuleset(allowedHosts).toUtf8());
    nft.closeWriteChannel();
    nft.waitForFinished(5000);
    if (nft.exitStatus() != QProcess::NormalExit || nft.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(nft.readAllStandardError()).trimmed();
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(allowedHosts)
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
#endif
}

void NetGate::drop() const
{
#ifdef Q_OS_LINUX
    QProcess::execute(QStringLiteral("nft"), {QStringLiteral("delete"), QStringLiteral("table"), QStringLiteral("inet"), QStringLiteral("focusos")});
#endif
}
