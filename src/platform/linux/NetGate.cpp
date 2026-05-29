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

#ifdef Q_OS_LINUX
namespace {

// Some sites can't be allowed by a single DNS lookup of their primary domain.
// YouTube is the canonical example: the page lives on youtube.com, but the
// video bytes stream from per-session hostnames like
// rr3---sn-xxxx.googlevideo.com that we can't resolve ahead of time, plus
// thumbnails/static assets on ytimg.com, ggpht.com, gstatic.com, etc. Pinning
// only youtube.com's IPs loads the page but the player just spins.
//
// So when the user allows a YouTube/Google host we expand the allowlist with:
//   1. the companion domains the player actually fetches, and
//   2. Google's long-held IPv4/IPv6 netblocks (googlevideo serves from
//      173.194/74.125/142.250/172.217 — these are stable allocations).
// CIDR strings are passed straight through to the nftables interval sets.
bool hostMatchesAny(const QString &host, const QStringList &needles)
{
    for (const QString &needle : needles) {
        if (host == needle || host.endsWith(QLatin1Char('.') + needle)) {
            return true;
        }
    }
    return false;
}

// Returns extra allowlist entries (companion domains + CIDR ranges) implied by
// a primary allowed host. Empty for hosts with no known companions.
QStringList serviceCompanions(const QString &host)
{
    static const QStringList googleHostRoots {
        QStringLiteral("youtube.com"),
        QStringLiteral("youtu.be"),
        QStringLiteral("youtube-nocookie.com"),
        QStringLiteral("ytimg.com"),
        QStringLiteral("googlevideo.com"),
        QStringLiteral("google.com"),
        QStringLiteral("gstatic.com")
    };
    if (!hostMatchesAny(host, googleHostRoots)) {
        return {};
    }

    return QStringList {
        // Domains the YouTube web player fetches beyond the page itself.
        QStringLiteral("www.youtube.com"),
        QStringLiteral("m.youtube.com"),
        QStringLiteral("youtubei.googleapis.com"),
        QStringLiteral("yt3.ggpht.com"),
        QStringLiteral("ggpht.com"),
        QStringLiteral("i.ytimg.com"),
        QStringLiteral("s.ytimg.com"),
        QStringLiteral("ytimg.com"),
        QStringLiteral("googlevideo.com"),
        QStringLiteral("googleapis.com"),
        QStringLiteral("gstatic.com"),
        QStringLiteral("www.gstatic.com"),
        QStringLiteral("fonts.gstatic.com"),
        QStringLiteral("play.google.com"),
        QStringLiteral("accounts.google.com"),
        QStringLiteral("accounts.youtube.com"),
        // Google's stable IPv4 netblocks — googlevideo edge servers live here.
        QStringLiteral("64.233.160.0/19"),
        QStringLiteral("66.102.0.0/20"),
        QStringLiteral("66.249.64.0/19"),
        QStringLiteral("72.14.192.0/18"),
        QStringLiteral("74.125.0.0/16"),
        QStringLiteral("108.177.0.0/17"),
        QStringLiteral("142.250.0.0/15"),
        QStringLiteral("172.217.0.0/16"),
        QStringLiteral("172.253.0.0/16"),
        QStringLiteral("173.194.0.0/16"),
        QStringLiteral("209.85.128.0/17"),
        QStringLiteral("216.58.192.0/19"),
        QStringLiteral("216.239.32.0/19"),
        // Google's IPv6 netblocks.
        QStringLiteral("2001:4860::/32"),
        QStringLiteral("2404:6800::/32"),
        QStringLiteral("2607:f8b0::/32"),
        QStringLiteral("2800:3f0::/32"),
        QStringLiteral("2a00:1450::/32"),
        QStringLiteral("2c0f:fb50::/32")
    };
}

bool runNftQuietly(const QString &nftPath, const QStringList &arguments, int timeoutMs = 3000)
{
    QProcess nft;
    nft.setProcessChannelMode(QProcess::MergedChannels);
    nft.start(nftPath, arguments);
    if (!nft.waitForFinished(timeoutMs)) {
        nft.kill();
        nft.waitForFinished(100);
        return false;
    }
    return nft.exitStatus() == QProcess::NormalExit && nft.exitCode() == 0;
}

void deleteFocusTableIfPresent(const QString &nftPath)
{
    if (!runNftQuietly(nftPath, {QStringLiteral("list"), QStringLiteral("table"), QStringLiteral("inet"), QStringLiteral("focusos")})) {
        return;
    }
    runNftQuietly(nftPath, {QStringLiteral("delete"), QStringLiteral("table"), QStringLiteral("inet"), QStringLiteral("focusos")});
}

// Replace the focusos table with `rules` via `nft -f -`. Shared by the
// allowlist apply path and the full-deny clamp so both get identical start /
// timeout / CAP_NET_ADMIN error handling.
bool loadRulesetIntoNft(const QString &rules, QString *errorMessage)
{
    const QString nftPath = QStandardPaths::findExecutable(QStringLiteral("nft"));
    if (nftPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("nftables is not installed; install nftables and enable the FocusOS network lock");
        }
        return false;
    }

    deleteFocusTableIfPresent(nftPath);

    QProcess nft;
    nft.start(nftPath, {QStringLiteral("-f"), QStringLiteral("-")});
    if (!nft.waitForStarted()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to start nft");
        }
        return false;
    }
    nft.write(rules.toUtf8());
    nft.closeWriteChannel();
    if (!nft.waitForFinished(5000)) {
        nft.kill();
        nft.waitForFinished(100);
        if (errorMessage) {
            *errorMessage = QStringLiteral("nftables did not finish applying the FocusOS network lock within 5 seconds");
        }
        return false;
    }
    if (nft.exitStatus() != QProcess::NormalExit || nft.exitCode() != 0) {
        QString stderrText = QString::fromUtf8(nft.readAllStandardError()).trimmed();
        // nft echoes the whole offending ruleset line (the allowlist can be
        // thousands of characters) plus a caret line under it. Keep just the
        // human-readable "… Error: …" diagnostic so the prompt doesn't run off
        // the screen.
        {
            const QStringList lines = stderrText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            QStringList kept;
            for (const QString &line : lines) {
                const QString trimmedLine = line.trimmed();
                if (trimmedLine.contains(QStringLiteral("Error:"), Qt::CaseInsensitive) ||
                    trimmedLine.contains(QStringLiteral("warning:"), Qt::CaseInsensitive)) {
                    kept.append(trimmedLine.left(200));
                }
            }
            if (!kept.isEmpty()) {
                stderrText = kept.mid(0, 2).join(QStringLiteral(" "));
            } else {
                stderrText = stderrText.left(200);
            }
        }
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
                    "  sudo setcap cap_net_admin,cap_net_raw+ep $(command -v nft)\n"
                    "or launch FocusOS via a small pkexec wrapper. Strict mode will not start until this is fixed.");
            } else {
                *errorMessage = stderrText.isEmpty()
                    ? QStringLiteral("nftables refused the ruleset (unknown error)")
                    : stderrText;
            }
        }
        return false;
    }
    return true;
}

} // namespace
#endif

QString NetGate::buildRuleset(const QStringList &allowedHosts) const
{
#ifdef Q_OS_LINUX
    QSet<QString> resolvedIpv4Addresses;
    QSet<QString> resolvedIpv6Addresses;

    // Expand the user's allowlist with companion domains / CIDR ranges for
    // known services (see serviceCompanions). Dedupe while preserving order.
    QStringList expandedEntries;
    QSet<QString> seenEntries;
    const auto addEntry = [&expandedEntries, &seenEntries](const QString &raw) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty() || seenEntries.contains(trimmed)) {
            return;
        }
        seenEntries.insert(trimmed);
        expandedEntries.append(trimmed);
    };
    for (const QString &entry : allowedHosts) {
        addEntry(entry);
        QString host = entry.trimmed().toLower();
        const QUrl url = QUrl::fromUserInput(host);
        if (!url.host().isEmpty()) {
            host = url.host().toLower();
        }
        for (const QString &companion : serviceCompanions(host)) {
            addEntry(companion);
        }
    }

    // Sanitize a single resolved/literal address into the right family bucket.
    // Strips IPv6 zone ids (nft's ipv6_addr set can't parse a "%iface" suffix)
    // and folds IPv4-mapped/-compatible forms (e.g. ::ffff:1.2.3.4) back to
    // plain IPv4 so they don't land in the ipv6 set carrying an embedded
    // dotted-quad that nft would reject.
    const auto insertAddress = [&resolvedIpv4Addresses, &resolvedIpv6Addresses](QHostAddress address) {
        if (address.isNull()) {
            return;
        }
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            resolvedIpv4Addresses.insert(address.toString());
            return;
        }
        if (address.protocol() != QAbstractSocket::IPv6Protocol) {
            return;
        }
        address.setScopeId(QString());
        QString text = address.toString();
        const int zone = text.indexOf(QLatin1Char('%'));
        if (zone >= 0) {
            text = text.left(zone);
        }
        if (text.contains(QLatin1Char('.'))) {
            bool ok = false;
            const quint32 mapped = address.toIPv4Address(&ok);
            if (ok) {
                resolvedIpv4Addresses.insert(QHostAddress(mapped).toString());
            }
            return;
        }
        resolvedIpv6Addresses.insert(text);
    };

    for (const QString &entry : expandedEntries) {
        QString host = entry.trimmed().toLower();
        if (host.isEmpty()) {
            continue;
        }

        // A genuine CIDR ("173.194.0.0/16", "2607:f8b0::/32") passes straight
        // into the nftables interval sets — no DNS needed. But a full URL
        // ("https://www.youtube.com/watch") or a host+path ("youtube.com/watch")
        // ALSO contains '/'. The old code only looked for a ':' before the
        // slash, so "https:" read as IPv6 and the whole raw URL was dumped into
        // the ipv6 set — that is the "syntax error, unexpected /" the network
        // routine hit. Validate it as an actual subnet first; if it isn't one,
        // fall through to URL/host extraction + DNS below.
        if (host.contains(QLatin1Char('/'))) {
            const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(host);
            if (!subnet.first.isNull() && subnet.second >= 0) {
                if (subnet.first.protocol() == QAbstractSocket::IPv6Protocol) {
                    resolvedIpv6Addresses.insert(host);
                } else {
                    resolvedIpv4Addresses.insert(host);
                }
                continue;
            }
        }

        const QUrl url = QUrl::fromUserInput(host);
        if (!url.host().isEmpty()) {
            host = url.host().toLower();
        } else {
            host = host.section(QLatin1Char('/'), 0, 0).section(QLatin1Char(':'), 0, 0);
        }

        const QHostAddress literalAddress(host);
        if (!literalAddress.isNull()) {
            insertAddress(literalAddress);
            continue;
        }

        // Resolve the host AND its www. counterpart. The extension allows
        // subdomains of an allowlisted host automatically (it matches host +
        // path), but nftables can only allow concrete IPs — so if we only
        // resolve the bare host the firewall silently drops the www. host the
        // browser is actually redirected to. qt.io is the canonical break:
        // allowing "qt.io" resolves one AWS IP, but the page lives on
        // www.qt.io (a HubSpot CDN on entirely different IPs), so it never
        // loads. Resolve both forms so the redirect target is reachable too.
        QStringList namesToResolve;
        namesToResolve.append(host);
        if (host.startsWith(QStringLiteral("www."))) {
            namesToResolve.append(host.mid(4));
        } else {
            namesToResolve.append(QStringLiteral("www.") + host);
        }
        for (const QString &name : namesToResolve) {
            const QHostInfo info = QHostInfo::fromName(name);
            for (const QHostAddress &address : info.addresses()) {
                insertAddress(address);
            }
        }
    }
#endif

    QString rules;
    rules += QStringLiteral("table inet focusos {\n");
    rules += QStringLiteral("  set allowed_ipv4 {\n");
    rules += QStringLiteral("    type ipv4_addr;\n");
    rules += QStringLiteral("    flags interval;\n");
    // auto-merge so a DNS-resolved /32 that falls inside one of the Google
    // CIDR blocks doesn't trip nftables' "interval overlaps" rejection.
    rules += QStringLiteral("    auto-merge;\n");
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
    rules += QStringLiteral("    auto-merge;\n");
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
    return loadRulesetIntoNft(buildRuleset(allowedHosts), errorMessage);
#else
    Q_UNUSED(allowedHosts)
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
#endif
}

bool NetGate::applyFullDeny(QString *errorMessage) const
{
#ifdef Q_OS_LINUX
    // No allowed sets, no DNS exception — only loopback survives. Anything that
    // tries to leave the machine hits `counter drop`. This is the clamp we
    // install when the browser blocker extension goes missing mid-session.
    QString rules;
    rules += QStringLiteral("table inet focusos {\n");
    rules += QStringLiteral("  chain output {\n");
    rules += QStringLiteral("    type filter hook output priority 0; policy drop;\n");
    rules += QStringLiteral("    oifname \"lo\" accept;\n");
    rules += QStringLiteral("    counter drop;\n");
    rules += QStringLiteral("  }\n");
    rules += QStringLiteral("}\n");
    return loadRulesetIntoNft(rules, errorMessage);
#else
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
        deleteFocusTableIfPresent(nftPath);
    }
#endif
}
