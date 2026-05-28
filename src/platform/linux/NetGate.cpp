#include "platform/linux/NetGate.h"

#ifdef Q_OS_LINUX
#include <QAbstractSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#endif

QString NetGate::buildRuleset(const QStringList &allowedHosts) const
{
#ifdef Q_OS_LINUX
    QSet<QString> resolvedIpv4Addresses;
    QSet<QString> resolvedIpv6Addresses;
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
        if (!literalAddress.isNull()) {
            if (literalAddress.protocol() == QAbstractSocket::IPv4Protocol) {
                resolvedIpv4Addresses.insert(literalAddress.toString());
            } else if (literalAddress.protocol() == QAbstractSocket::IPv6Protocol) {
                resolvedIpv6Addresses.insert(literalAddress.toString());
            }
            continue;
        }

        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress &address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                resolvedIpv4Addresses.insert(address.toString());
            } else if (address.protocol() == QAbstractSocket::IPv6Protocol) {
                resolvedIpv6Addresses.insert(address.toString());
            }
        }
    }
#endif

    QString rules;
    rules += QStringLiteral("table inet focusos {\n");
    rules += QStringLiteral("  set allowed_ipv4 {\n");
    rules += QStringLiteral("    type ipv4_addr;\n");
    rules += QStringLiteral("    flags interval;\n");
#ifdef Q_OS_LINUX
    if (!resolvedIpv4Addresses.isEmpty()) {
        QStringList sortedAddresses;
        sortedAddresses.reserve(resolvedIpv4Addresses.size());
        for (const QString &address : resolvedIpv4Addresses) {
            sortedAddresses.append(address);
        }
        sortedAddresses.sort();
        rules += QStringLiteral("    elements = { %1 };\n").arg(sortedAddresses.join(QStringLiteral(", ")));
    }
#endif
    rules += QStringLiteral("  }\n");
    rules += QStringLiteral("  set allowed_ipv6 {\n");
    rules += QStringLiteral("    type ipv6_addr;\n");
    rules += QStringLiteral("    flags interval;\n");
#ifdef Q_OS_LINUX
    if (!resolvedIpv6Addresses.isEmpty()) {
        QStringList sortedAddresses;
        sortedAddresses.reserve(resolvedIpv6Addresses.size());
        for (const QString &address : resolvedIpv6Addresses) {
            sortedAddresses.append(address);
        }
        sortedAddresses.sort();
        rules += QStringLiteral("    elements = { %1 };\n").arg(sortedAddresses.join(QStringLiteral(", ")));
    }
#endif
    rules += QStringLiteral("  }\n");
    rules += QStringLiteral("  chain output {\n");
    rules += QStringLiteral("    type filter hook output priority 0; policy drop;\n");
    rules += QStringLiteral("    oifname \"lo\" accept;\n");
    rules += QStringLiteral("    meta l4proto { tcp, udp } th dport 53 accept;\n");
    rules += QStringLiteral("    ip daddr @allowed_ipv4 accept;\n");
    rules += QStringLiteral("    ip6 daddr @allowed_ipv6 accept;\n");
    rules += QStringLiteral("    counter drop;\n");
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
    const QString nftPath = QStandardPaths::findExecutable(QStringLiteral("nft"));
    if (nftPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("nftables is not installed; install nftables and enable the FocusOS network lock");
        }
        return false;
    }

    QProcess::execute(nftPath, {QStringLiteral("delete"), QStringLiteral("table"), QStringLiteral("inet"), QStringLiteral("focusos")});

    QProcess nft;
    nft.start(nftPath, {QStringLiteral("-f"), QStringLiteral("-")});
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
        QString stderrText = QString::fromUtf8(nft.readAllStandardError()).trimmed();
        // nftables needs CAP_NET_ADMIN — without it, the kernel rejects the
        // netlink cache load with EPERM. Translate that into something the
        // user can actually act on.
        const bool looksUnprivileged = stderrText.contains(QStringLiteral("Operation not permitted"), Qt::CaseInsensitive) ||
                                       stderrText.contains(QStringLiteral("cache initialization failed"), Qt::CaseInsensitive) ||
                                       stderrText.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive);
        if (errorMessage) {
            if (looksUnprivileged) {
                *errorMessage = QStringLiteral(
                    "FocusOS needs CAP_NET_ADMIN to install firewall rules. Either run\n"
                    "  sudo setcap cap_net_admin,cap_net_raw+ep $(command -v focusos)\n"
                    "or launch FocusOS via a small pkexec wrapper. The routine can still start without the lock.");
            } else {
                *errorMessage = stderrText.isEmpty()
                    ? QStringLiteral("nftables refused the ruleset (unknown error)")
                    : stderrText;
            }
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
    const QString nftPath = QStandardPaths::findExecutable(QStringLiteral("nft"));
    if (!nftPath.isEmpty()) {
        QProcess::execute(nftPath, {QStringLiteral("delete"), QStringLiteral("table"), QStringLiteral("inet"), QStringLiteral("focusos")});
    }
#endif
}
